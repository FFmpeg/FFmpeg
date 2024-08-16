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
 * implementing an object detecting filter using deep learning networks.
 */

#include "libavutil/file_open.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "filters.h"
#include "dnn_filter_common.h"
#include "video.h"
#include "libavutil/time.h"
#include "libavutil/avstring.h"
#include "libavutil/detection_bbox.h"
#include "libavutil/fifo.h"

typedef enum {
    DDMT_SSD,
    DDMT_YOLOV1V2,
    DDMT_YOLOV3,
    DDMT_YOLOV4
} DNNDetectionModelType;

typedef struct DnnDetectContext {
    const AVClass *class;
    DnnContext dnnctx;
    float confidence;
    char *labels_filename;
    char **labels;
    int label_count;
    DNNDetectionModelType model_type;
    int cell_w;
    int cell_h;
    int nb_classes;
    AVFifo *bboxes_fifo;
    int scale_width;
    int scale_height;
    char *anchors_str;
    float *anchors;
    int nb_anchor;
} DnnDetectContext;

#define OFFSET(x) offsetof(DnnDetectContext, dnnctx.x)
#define OFFSET2(x) offsetof(DnnDetectContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption dnn_detect_options[] = {
    { "dnn_backend", "DNN backend",                OFFSET(backend_type),     AV_OPT_TYPE_INT,       { .i64 = DNN_OV },    INT_MIN, INT_MAX, FLAGS, .unit = "backend" },
#if (CONFIG_LIBTENSORFLOW == 1)
    { "tensorflow",  "tensorflow backend flag",    0,                        AV_OPT_TYPE_CONST,     { .i64 = DNN_TF },    0, 0, FLAGS, .unit = "backend" },
#endif
#if (CONFIG_LIBOPENVINO == 1)
    { "openvino",    "openvino backend flag",      0,                        AV_OPT_TYPE_CONST,     { .i64 = DNN_OV },    0, 0, FLAGS, .unit = "backend" },
#endif
    { "confidence",  "threshold of confidence",    OFFSET2(confidence),      AV_OPT_TYPE_FLOAT,     { .dbl = 0.5 },  0, 1, FLAGS},
    { "labels",      "path to labels file",        OFFSET2(labels_filename), AV_OPT_TYPE_STRING,    { .str = NULL }, 0, 0, FLAGS },
    { "model_type",  "DNN detection model type",   OFFSET2(model_type),      AV_OPT_TYPE_INT,       { .i64 = DDMT_SSD },    INT_MIN, INT_MAX, FLAGS, .unit = "model_type" },
        { "ssd",     "output shape [1, 1, N, 7]",  0,                        AV_OPT_TYPE_CONST,       { .i64 = DDMT_SSD },    0, 0, FLAGS, .unit = "model_type" },
        { "yolo",    "output shape [1, N*Cx*Cy*DetectionBox]",  0,           AV_OPT_TYPE_CONST,       { .i64 = DDMT_YOLOV1V2 },    0, 0, FLAGS, .unit = "model_type" },
        { "yolov3",  "outputs shape [1, N*D, Cx, Cy]",  0,                   AV_OPT_TYPE_CONST,       { .i64 = DDMT_YOLOV3 },      0, 0, FLAGS, .unit = "model_type" },
        { "yolov4",  "outputs shape [1, N*D, Cx, Cy]",  0,                   AV_OPT_TYPE_CONST,       { .i64 = DDMT_YOLOV4 },    0, 0, FLAGS, .unit = "model_type" },
    { "cell_w",      "cell width",                 OFFSET2(cell_w),          AV_OPT_TYPE_INT,       { .i64 = 0 },    0, INTMAX_MAX, FLAGS },
    { "cell_h",      "cell height",                OFFSET2(cell_h),          AV_OPT_TYPE_INT,       { .i64 = 0 },    0, INTMAX_MAX, FLAGS },
    { "nb_classes",  "The number of class",        OFFSET2(nb_classes),      AV_OPT_TYPE_INT,       { .i64 = 0 },    0, INTMAX_MAX, FLAGS },
    { "anchors",     "anchors, splited by '&'",    OFFSET2(anchors_str),         AV_OPT_TYPE_STRING,    { .str = NULL }, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DNN_DEFINE_CLASS(dnn_detect, DNN_TF | DNN_OV);

static inline float sigmoid(float x) {
    return 1.f / (1.f + exp(-x));
}

static inline float linear(float x) {
    return x;
}

static int dnn_detect_get_label_id(int nb_classes, int cell_size, float *label_data)
{
    float max_prob = 0;
    int label_id = 0;
    for (int i = 0; i < nb_classes; i++) {
        if (label_data[i * cell_size] > max_prob) {
            max_prob = label_data[i * cell_size];
            label_id = i;
        }
    }
    return label_id;
}

static int dnn_detect_parse_anchors(char *anchors_str, float **anchors)
{
    char *saveptr = NULL, *token;
    float *anchors_buf;
    int nb_anchor = 0, i = 0;
    while(anchors_str[i] != '\0') {
        if(anchors_str[i] == '&')
            nb_anchor++;
        i++;
    }
    nb_anchor++;
    anchors_buf = av_mallocz(nb_anchor * sizeof(**anchors));
    if (!anchors_buf) {
        return 0;
    }
    for (int i = 0; i < nb_anchor; i++) {
        token = av_strtok(anchors_str, "&", &saveptr);
        if (!token) {
            av_freep(&anchors_buf);
            return 0;
        }
        anchors_buf[i] = strtof(token, NULL);
        anchors_str = NULL;
    }
    *anchors = anchors_buf;
    return nb_anchor;
}

/* Calculate Intersection Over Union */
static float dnn_detect_IOU(AVDetectionBBox *bbox1, AVDetectionBBox *bbox2)
{
    float overlapping_width = FFMIN(bbox1->x + bbox1->w, bbox2->x + bbox2->w) - FFMAX(bbox1->x, bbox2->x);
    float overlapping_height = FFMIN(bbox1->y + bbox1->h, bbox2->y + bbox2->h) - FFMAX(bbox1->y, bbox2->y);
    float intersection_area =
        (overlapping_width < 0 || overlapping_height < 0) ? 0 : overlapping_height * overlapping_width;
    float union_area = bbox1->w * bbox1->h + bbox2->w * bbox2->h - intersection_area;
    return intersection_area / union_area;
}

static int dnn_detect_parse_yolo_output(AVFrame *frame, DNNData *output, int output_index,
                                      AVFilterContext *filter_ctx)
{
    DnnDetectContext *ctx = filter_ctx->priv;
    float conf_threshold = ctx->confidence;
    int detection_boxes, box_size;
    int cell_w = 0, cell_h = 0, scale_w = 0, scale_h = 0;
    int nb_classes = ctx->nb_classes;
    float *output_data = output[output_index].data;
    float *anchors = ctx->anchors;
    AVDetectionBBox *bbox;
    float (*post_process_raw_data)(float x) = linear;
    int is_NHWC = 0;

    if (ctx->model_type == DDMT_YOLOV1V2) {
        cell_w = ctx->cell_w;
        cell_h = ctx->cell_h;
        scale_w = cell_w;
        scale_h = cell_h;
    } else {
        if (output[output_index].dims[2] != output[output_index].dims[3] &&
            output[output_index].dims[2] == output[output_index].dims[1]) {
            is_NHWC = 1;
            cell_w = output[output_index].dims[2];
            cell_h = output[output_index].dims[1];
        } else {
            cell_w = output[output_index].dims[3];
            cell_h = output[output_index].dims[2];
        }
        scale_w = ctx->scale_width;
        scale_h = ctx->scale_height;
    }
    box_size = nb_classes + 5;

    switch (ctx->model_type) {
    case DDMT_YOLOV1V2:
    case DDMT_YOLOV3:
        post_process_raw_data = linear;
        break;
    case DDMT_YOLOV4:
        post_process_raw_data = sigmoid;
         break;
    }

    if (!cell_h || !cell_w) {
        av_log(filter_ctx, AV_LOG_ERROR, "cell_w and cell_h are detected\n");
        return AVERROR(EINVAL);
    }

    if (!nb_classes) {
        av_log(filter_ctx, AV_LOG_ERROR, "nb_classes is not set\n");
        return AVERROR(EINVAL);
    }

    if (!anchors) {
        av_log(filter_ctx, AV_LOG_ERROR, "anchors is not set\n");
        return AVERROR(EINVAL);
    }

    if (output[output_index].dims[1] * output[output_index].dims[2] *
            output[output_index].dims[3] % (box_size * cell_w * cell_h)) {
        av_log(filter_ctx, AV_LOG_ERROR, "wrong cell_w, cell_h or nb_classes\n");
        return AVERROR(EINVAL);
    }
    detection_boxes = output[output_index].dims[1] *
                      output[output_index].dims[2] *
                      output[output_index].dims[3] / box_size / cell_w / cell_h;

    anchors = anchors + (detection_boxes * output_index * 2);
    /**
     * find all candidate bbox
     * yolo output can be reshaped to [B, N*D, Cx, Cy]
     * Detection box 'D' has format [`x`, `y`, `h`, `w`, `box_score`, `class_no_1`, ...,]
     **/
    for (int box_id = 0; box_id < detection_boxes; box_id++) {
        for (int cx = 0; cx < cell_w; cx++)
            for (int cy = 0; cy < cell_h; cy++) {
                float x, y, w, h, conf;
                float *detection_boxes_data;
                int label_id;

                if (is_NHWC) {
                    detection_boxes_data = output_data +
                        ((cy * cell_w + cx) * detection_boxes + box_id) * box_size;
                    conf = post_process_raw_data(detection_boxes_data[4]);
                } else {
                    detection_boxes_data = output_data + box_id * box_size * cell_w * cell_h;
                    conf = post_process_raw_data(
                                detection_boxes_data[cy * cell_w + cx + 4 * cell_w * cell_h]);
                }

                if (is_NHWC) {
                    x = post_process_raw_data(detection_boxes_data[0]);
                    y = post_process_raw_data(detection_boxes_data[1]);
                    w = detection_boxes_data[2];
                    h = detection_boxes_data[3];
                    label_id = dnn_detect_get_label_id(ctx->nb_classes, 1, detection_boxes_data + 5);
                    conf = conf * post_process_raw_data(detection_boxes_data[label_id + 5]);
                } else {
                    x = post_process_raw_data(detection_boxes_data[cy * cell_w + cx]);
                    y = post_process_raw_data(detection_boxes_data[cy * cell_w + cx + cell_w * cell_h]);
                    w = detection_boxes_data[cy * cell_w + cx + 2 * cell_w * cell_h];
                    h = detection_boxes_data[cy * cell_w + cx + 3 * cell_w * cell_h];
                    label_id = dnn_detect_get_label_id(ctx->nb_classes, cell_w * cell_h,
                        detection_boxes_data + cy * cell_w + cx + 5 * cell_w * cell_h);
                    conf = conf * post_process_raw_data(
                                detection_boxes_data[cy * cell_w + cx + (label_id + 5) * cell_w * cell_h]);
                }
                if (conf < conf_threshold) {
                    continue;
                }

                bbox = av_mallocz(sizeof(*bbox));
                if (!bbox)
                    return AVERROR(ENOMEM);

                bbox->w = exp(w) * anchors[box_id * 2] * frame->width / scale_w;
                bbox->h = exp(h) * anchors[box_id * 2 + 1] * frame->height / scale_h;
                bbox->x = (cx + x) / cell_w * frame->width - bbox->w / 2;
                bbox->y = (cy + y) / cell_h * frame->height - bbox->h / 2;
                bbox->detect_confidence = av_make_q((int)(conf * 10000), 10000);
                if (ctx->labels && label_id < ctx->label_count) {
                    av_strlcpy(bbox->detect_label, ctx->labels[label_id], sizeof(bbox->detect_label));
                } else {
                    snprintf(bbox->detect_label, sizeof(bbox->detect_label), "%d", label_id);
                }

                if (av_fifo_write(ctx->bboxes_fifo, &bbox, 1) < 0) {
                    av_freep(&bbox);
                    return AVERROR(ENOMEM);
                }
                bbox = NULL;
            }
    }
    return 0;
}

static int dnn_detect_fill_side_data(AVFrame *frame, AVFilterContext *filter_ctx)
{
    DnnDetectContext *ctx = filter_ctx->priv;
    float conf_threshold = ctx->confidence;
    AVDetectionBBox *bbox;
    int nb_bboxes = 0;
    AVDetectionBBoxHeader *header;
    if (av_fifo_can_read(ctx->bboxes_fifo) == 0) {
        av_log(filter_ctx, AV_LOG_VERBOSE, "nothing detected in this frame.\n");
        return 0;
    }

    /* remove overlap bboxes */
    for (int i = 0; i < av_fifo_can_read(ctx->bboxes_fifo); i++){
        av_fifo_peek(ctx->bboxes_fifo, &bbox, 1, i);
        for (int j = 0; j < av_fifo_can_read(ctx->bboxes_fifo); j++) {
            AVDetectionBBox *overlap_bbox;
            av_fifo_peek(ctx->bboxes_fifo, &overlap_bbox, 1, j);
            if (!strcmp(bbox->detect_label, overlap_bbox->detect_label) &&
                av_cmp_q(bbox->detect_confidence, overlap_bbox->detect_confidence) < 0 &&
                dnn_detect_IOU(bbox, overlap_bbox) >= conf_threshold) {
                    bbox->classify_count = -1; // bad result
                    nb_bboxes++;
                    break;
                }
        }
    }
    nb_bboxes = av_fifo_can_read(ctx->bboxes_fifo) - nb_bboxes;
    header = av_detection_bbox_create_side_data(frame, nb_bboxes);
    if (!header) {
        av_log(filter_ctx, AV_LOG_ERROR, "failed to create side data with %d bounding boxes\n", nb_bboxes);
         return -1;
     }
    av_strlcpy(header->source, ctx->dnnctx.model_filename, sizeof(header->source));

    while(av_fifo_can_read(ctx->bboxes_fifo)) {
        AVDetectionBBox *candidate_bbox;
        av_fifo_read(ctx->bboxes_fifo, &candidate_bbox, 1);

        if (nb_bboxes > 0 && candidate_bbox->classify_count != -1) {
            bbox = av_get_detection_bbox(header, header->nb_bboxes - nb_bboxes);
            memcpy(bbox, candidate_bbox, sizeof(*bbox));
            nb_bboxes--;
        }
        av_freep(&candidate_bbox);
    }
    return 0;
}

static int dnn_detect_post_proc_yolo(AVFrame *frame, DNNData *output, AVFilterContext *filter_ctx)
{
    int ret = 0;
    ret = dnn_detect_parse_yolo_output(frame, output, 0, filter_ctx);
    if (ret < 0)
        return ret;
    ret = dnn_detect_fill_side_data(frame, filter_ctx);
    if (ret < 0)
        return ret;
    return 0;
}

static int dnn_detect_post_proc_yolov3(AVFrame *frame, DNNData *output,
                                       AVFilterContext *filter_ctx, int nb_outputs)
{
    int ret = 0;
    for (int i = 0; i < nb_outputs; i++) {
        ret = dnn_detect_parse_yolo_output(frame, output, i, filter_ctx);
        if (ret < 0)
            return ret;
    }
    ret = dnn_detect_fill_side_data(frame, filter_ctx);
    if (ret < 0)
        return ret;
    return 0;
}

static int dnn_detect_post_proc_ssd(AVFrame *frame, DNNData *output, int nb_outputs,
                                    AVFilterContext *filter_ctx)
{
    DnnDetectContext *ctx = filter_ctx->priv;
    float conf_threshold = ctx->confidence;
    int proposal_count = 0;
    int detect_size = 0;
    float *detections = NULL, *labels = NULL;
    int nb_bboxes = 0;
    AVDetectionBBoxHeader *header;
    AVDetectionBBox *bbox;
    int scale_w = ctx->scale_width;
    int scale_h = ctx->scale_height;

    if (nb_outputs == 1 && output->dims[3] == 7) {
        proposal_count = output->dims[2];
        detect_size = output->dims[3];
        detections = output->data;
    } else if (nb_outputs == 2 && output[0].dims[3] == 5) {
        proposal_count = output[0].dims[2];
        detect_size = output[0].dims[3];
        detections = output[0].data;
        labels = output[1].data;
    } else if (nb_outputs == 2 && output[1].dims[3] == 5) {
        proposal_count = output[1].dims[2];
        detect_size = output[1].dims[3];
        detections = output[1].data;
        labels = output[0].data;
    } else {
        av_log(filter_ctx, AV_LOG_ERROR, "Model output shape doesn't match ssd requirement.\n");
        return AVERROR(EINVAL);
    }

    if (proposal_count == 0)
        return 0;

    for (int i = 0; i < proposal_count; ++i) {
        float conf;
        if (nb_outputs == 1)
            conf = detections[i * detect_size + 2];
        else
            conf = detections[i * detect_size + 4];
        if (conf < conf_threshold) {
            continue;
        }
        nb_bboxes++;
    }

    if (nb_bboxes == 0) {
        av_log(filter_ctx, AV_LOG_VERBOSE, "nothing detected in this frame.\n");
        return 0;
    }

    header = av_detection_bbox_create_side_data(frame, nb_bboxes);
    if (!header) {
        av_log(filter_ctx, AV_LOG_ERROR, "failed to create side data with %d bounding boxes\n", nb_bboxes);
        return -1;
    }

    av_strlcpy(header->source, ctx->dnnctx.model_filename, sizeof(header->source));

    for (int i = 0; i < proposal_count; ++i) {
        int av_unused image_id = (int)detections[i * detect_size + 0];
        int label_id;
        float conf, x0, y0, x1, y1;

        if (nb_outputs == 1) {
            label_id = (int)detections[i * detect_size + 1];
            conf = detections[i * detect_size + 2];
            x0   = detections[i * detect_size + 3];
            y0   = detections[i * detect_size + 4];
            x1   = detections[i * detect_size + 5];
            y1   = detections[i * detect_size + 6];
        } else {
            label_id = (int)labels[i];
            x0     =      detections[i * detect_size] / scale_w;
            y0     =      detections[i * detect_size + 1] / scale_h;
            x1     =      detections[i * detect_size + 2] / scale_w;
            y1     =      detections[i * detect_size + 3] / scale_h;
            conf   =      detections[i * detect_size + 4];
        }

        if (conf < conf_threshold) {
            continue;
        }

        bbox = av_get_detection_bbox(header, header->nb_bboxes - nb_bboxes);
        bbox->x = (int)(x0 * frame->width);
        bbox->w = (int)(x1 * frame->width) - bbox->x;
        bbox->y = (int)(y0 * frame->height);
        bbox->h = (int)(y1 * frame->height) - bbox->y;

        bbox->detect_confidence = av_make_q((int)(conf * 10000), 10000);
        bbox->classify_count = 0;

        if (ctx->labels && label_id < ctx->label_count) {
            av_strlcpy(bbox->detect_label, ctx->labels[label_id], sizeof(bbox->detect_label));
        } else {
            snprintf(bbox->detect_label, sizeof(bbox->detect_label), "%d", label_id);
        }

        nb_bboxes--;
        if (nb_bboxes == 0) {
            break;
        }
    }
    return 0;
}

static int dnn_detect_post_proc_ov(AVFrame *frame, DNNData *output, int nb_outputs,
                                   AVFilterContext *filter_ctx)
{
    AVFrameSideData *sd;
    DnnDetectContext *ctx = filter_ctx->priv;
    int ret = 0;

    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DETECTION_BBOXES);
    if (sd) {
        av_log(filter_ctx, AV_LOG_ERROR, "already have bounding boxes in side data.\n");
        return -1;
    }

    switch (ctx->model_type) {
    case DDMT_SSD:
        ret = dnn_detect_post_proc_ssd(frame, output, nb_outputs, filter_ctx);
        if (ret < 0)
            return ret;
        break;
    case DDMT_YOLOV1V2:
        ret = dnn_detect_post_proc_yolo(frame, output, filter_ctx);
        if (ret < 0)
            return ret;
        break;
    case DDMT_YOLOV3:
    case DDMT_YOLOV4:
        ret = dnn_detect_post_proc_yolov3(frame, output, filter_ctx, nb_outputs);
        if (ret < 0)
            return ret;
        break;
    }
    return 0;
}

static int dnn_detect_post_proc_tf(AVFrame *frame, DNNData *output, AVFilterContext *filter_ctx)
{
    DnnDetectContext *ctx = filter_ctx->priv;
    int proposal_count;
    float conf_threshold = ctx->confidence;
    float *conf, *position, *label_id, x0, y0, x1, y1;
    int nb_bboxes = 0;
    AVFrameSideData *sd;
    AVDetectionBBox *bbox;
    AVDetectionBBoxHeader *header;

    proposal_count = *(float *)(output[0].data);
    conf           = output[1].data;
    position       = output[3].data;
    label_id       = output[2].data;

    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DETECTION_BBOXES);
    if (sd) {
        av_log(filter_ctx, AV_LOG_ERROR, "already have dnn bounding boxes in side data.\n");
        return -1;
    }

    for (int i = 0; i < proposal_count; ++i) {
        if (conf[i] < conf_threshold)
            continue;
        nb_bboxes++;
    }

    if (nb_bboxes == 0) {
        av_log(filter_ctx, AV_LOG_VERBOSE, "nothing detected in this frame.\n");
        return 0;
    }

    header = av_detection_bbox_create_side_data(frame, nb_bboxes);
    if (!header) {
        av_log(filter_ctx, AV_LOG_ERROR, "failed to create side data with %d bounding boxes\n", nb_bboxes);
        return -1;
    }

    av_strlcpy(header->source, ctx->dnnctx.model_filename, sizeof(header->source));

    for (int i = 0; i < proposal_count; ++i) {
        y0 = position[i * 4];
        x0 = position[i * 4 + 1];
        y1 = position[i * 4 + 2];
        x1 = position[i * 4 + 3];

        bbox = av_get_detection_bbox(header, i);

        if (conf[i] < conf_threshold) {
            continue;
        }

        bbox->x = (int)(x0 * frame->width);
        bbox->w = (int)(x1 * frame->width) - bbox->x;
        bbox->y = (int)(y0 * frame->height);
        bbox->h = (int)(y1 * frame->height) - bbox->y;

        bbox->detect_confidence = av_make_q((int)(conf[i] * 10000), 10000);
        bbox->classify_count = 0;

        if (ctx->labels && label_id[i] < ctx->label_count) {
            av_strlcpy(bbox->detect_label, ctx->labels[(int)label_id[i]], sizeof(bbox->detect_label));
        } else {
            snprintf(bbox->detect_label, sizeof(bbox->detect_label), "%d", (int)label_id[i]);
        }

        nb_bboxes--;
        if (nb_bboxes == 0) {
            break;
        }
    }
    return 0;
}

static int dnn_detect_post_proc(AVFrame *frame, DNNData *output, uint32_t nb, AVFilterContext *filter_ctx)
{
    DnnDetectContext *ctx = filter_ctx->priv;
    DnnContext *dnn_ctx = &ctx->dnnctx;
    switch (dnn_ctx->backend_type) {
    case DNN_OV:
        return dnn_detect_post_proc_ov(frame, output, nb, filter_ctx);
    case DNN_TF:
        return dnn_detect_post_proc_tf(frame, output, filter_ctx);
    default:
        avpriv_report_missing_feature(filter_ctx, "Current dnn backend does not support detect filter\n");
        return AVERROR(EINVAL);
    }
}

static void free_detect_labels(DnnDetectContext *ctx)
{
    for (int i = 0; i < ctx->label_count; i++) {
        av_freep(&ctx->labels[i]);
    }
    ctx->label_count = 0;
    av_freep(&ctx->labels);
}

static int read_detect_label_file(AVFilterContext *context)
{
    int line_len;
    FILE *file;
    DnnDetectContext *ctx = context->priv;

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

static int check_output_nb(DnnDetectContext *ctx, DNNBackendType backend_type, int output_nb)
{
    switch(backend_type) {
    case DNN_TF:
        if (output_nb != 4) {
            av_log(ctx, AV_LOG_ERROR, "Only support tensorflow detect model with 4 outputs, \
                                       but get %d instead\n", output_nb);
            return AVERROR(EINVAL);
        }
        return 0;
    case DNN_OV:
        return 0;
    default:
        avpriv_report_missing_feature(ctx, "Dnn detect filter does not support current backend\n");
        return AVERROR(EINVAL);
    }
    return 0;
}

static av_cold int dnn_detect_init(AVFilterContext *context)
{
    DnnDetectContext *ctx = context->priv;
    DnnContext *dnn_ctx = &ctx->dnnctx;
    int ret;

    ret = ff_dnn_init(&ctx->dnnctx, DFT_ANALYTICS_DETECT, context);
    if (ret < 0)
        return ret;
    ret = check_output_nb(ctx, dnn_ctx->backend_type, dnn_ctx->nb_outputs);
    if (ret < 0)
        return ret;
    ctx->bboxes_fifo = av_fifo_alloc2(1, sizeof(AVDetectionBBox *), AV_FIFO_FLAG_AUTO_GROW);
    if (!ctx->bboxes_fifo)
        return AVERROR(ENOMEM);
    ff_dnn_set_detect_post_proc(&ctx->dnnctx, dnn_detect_post_proc);

    if (ctx->labels_filename) {
        return read_detect_label_file(context);
    }
    if (ctx->anchors_str) {
        ret = dnn_detect_parse_anchors(ctx->anchors_str, &ctx->anchors);
        if (!ctx->anchors) {
            av_log(context, AV_LOG_ERROR, "failed to parse anchors_str\n");
            return AVERROR(EINVAL);
        }
        ctx->nb_anchor = ret;
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

static int dnn_detect_flush_frame(AVFilterLink *outlink, int64_t pts, int64_t *out_pts)
{
    DnnDetectContext *ctx = outlink->src->priv;
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

static int dnn_detect_activate(AVFilterContext *filter_ctx)
{
    AVFilterLink *inlink = filter_ctx->inputs[0];
    AVFilterLink *outlink = filter_ctx->outputs[0];
    DnnDetectContext *ctx = filter_ctx->priv;
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
            if (ff_dnn_execute_model(&ctx->dnnctx, in, NULL) != 0) {
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
            ret = dnn_detect_flush_frame(outlink, pts, &out_pts);
            ff_outlink_set_status(outlink, status, out_pts);
            return ret;
        }
    }

    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return 0;
}

static av_cold void dnn_detect_uninit(AVFilterContext *context)
{
    DnnDetectContext *ctx = context->priv;
    AVDetectionBBox *bbox;
    ff_dnn_uninit(&ctx->dnnctx);
    if (ctx->bboxes_fifo) {
        while (av_fifo_can_read(ctx->bboxes_fifo)) {
            av_fifo_read(ctx->bboxes_fifo, &bbox, 1);
            av_freep(&bbox);
        }
        av_fifo_freep2(&ctx->bboxes_fifo);
    }
    av_freep(&ctx->anchors);
    free_detect_labels(ctx);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *context     = inlink->dst;
    DnnDetectContext *ctx = context->priv;
    DNNData model_input;
    int ret, width_idx, height_idx;

    ret = ff_dnn_get_input(&ctx->dnnctx, &model_input);
    if (ret != 0) {
        av_log(ctx, AV_LOG_ERROR, "could not get input from the model\n");
        return ret;
    }
    width_idx = dnn_get_width_idx_by_layout(model_input.layout);
    height_idx = dnn_get_height_idx_by_layout(model_input.layout);
    ctx->scale_width = model_input.dims[width_idx] == -1 ? inlink->w :
        model_input.dims[width_idx];
    ctx->scale_height = model_input.dims[height_idx] ==  -1 ? inlink->h :
        model_input.dims[height_idx];

    return 0;
}

static const AVFilterPad dnn_detect_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
    },
};

const AVFilter ff_vf_dnn_detect = {
    .name          = "dnn_detect",
    .description   = NULL_IF_CONFIG_SMALL("Apply DNN detect filter to the input."),
    .priv_size     = sizeof(DnnDetectContext),
    .preinit       = ff_dnn_filter_init_child_class,
    .init          = dnn_detect_init,
    .uninit        = dnn_detect_uninit,
    FILTER_INPUTS(dnn_detect_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &dnn_detect_class,
    .activate      = dnn_detect_activate,
};
