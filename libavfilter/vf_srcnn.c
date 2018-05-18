/*
 * Copyright (c) 2018 Sergey Lavrushkin
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
 * Filter implementing image super-resolution using deep convolutional networks.
 * https://arxiv.org/abs/1501.00092
 */

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "libavutil/opt.h"
#include "vf_srcnn.h"
#include "libavformat/avio.h"

typedef struct Convolution
{
    double* kernel;
    double* biases;
    int32_t size, input_channels, output_channels;
} Convolution;

typedef struct SRCNNContext {
    const AVClass *class;

    /// SRCNN convolutions
    struct Convolution conv1, conv2, conv3;
    /// Path to binary file with kernels specifications
    char* config_file_path;
    /// Buffers for network input/output and feature maps
    double* input_output_buf;
    double* conv1_buf;
    double* conv2_buf;
} SRCNNContext;


#define OFFSET(x) offsetof(SRCNNContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption srcnn_options[] = {
    { "config_file", "path to configuration file with network parameters", OFFSET(config_file_path), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(srcnn);

#define CHECK_FILE_SIZE(file_size, srcnn_size, avio_context)    if (srcnn_size > file_size){ \
                                                                    av_log(context, AV_LOG_ERROR, "error reading configuration file\n");\
                                                                    avio_closep(&avio_context); \
                                                                    return AVERROR(EIO); \
                                                                }

#define CHECK_ALLOCATION(call, end_call)    if (call){ \
                                                av_log(context, AV_LOG_ERROR, "could not allocate memory for convolutions\n"); \
                                                end_call; \
                                                return AVERROR(ENOMEM); \
                                            }

static int allocate_read_conv_data(Convolution* conv, AVIOContext* config_file_context)
{
    int32_t kernel_size = conv->output_channels * conv->size * conv->size * conv->input_channels;
    int32_t i;

    conv->kernel = av_malloc(kernel_size * sizeof(double));
    if (!conv->kernel){
        return AVERROR(ENOMEM);
    }
    for (i = 0; i < kernel_size; ++i){
        conv->kernel[i] = av_int2double(avio_rl64(config_file_context));
    }

    conv->biases = av_malloc(conv->output_channels * sizeof(double));
    if (!conv->biases){
        return AVERROR(ENOMEM);
    }
    for (i = 0; i < conv->output_channels; ++i){
        conv->biases[i] = av_int2double(avio_rl64(config_file_context));
    }

    return 0;
}

static int allocate_copy_conv_data(Convolution* conv, const double* kernel, const double* biases)
{
    int32_t kernel_size = conv->output_channels * conv->size * conv->size * conv->input_channels;

    conv->kernel = av_malloc(kernel_size * sizeof(double));
    if (!conv->kernel){
        return AVERROR(ENOMEM);
    }
    memcpy(conv->kernel, kernel, kernel_size * sizeof(double));

    conv->biases = av_malloc(conv->output_channels * sizeof(double));
    if (!conv->kernel){
        return AVERROR(ENOMEM);
    }
    memcpy(conv->biases, biases, conv->output_channels * sizeof(double));

    return 0;
}

static av_cold int init(AVFilterContext* context)
{
    SRCNNContext *srcnn_context = context->priv;
    AVIOContext* config_file_context;
    int64_t file_size, srcnn_size;

    /// Check specified confguration file name and read network weights from it
    if (!srcnn_context->config_file_path){
        av_log(context, AV_LOG_INFO, "configuration file for network was not specified, using default weights for x2 upsampling\n");

        /// Create convolution kernels and copy default weights
        srcnn_context->conv1.input_channels = 1;
        srcnn_context->conv1.output_channels = 64;
        srcnn_context->conv1.size = 9;
        CHECK_ALLOCATION(allocate_copy_conv_data(&srcnn_context->conv1, conv1_kernel, conv1_biases), )

        srcnn_context->conv2.input_channels = 64;
        srcnn_context->conv2.output_channels = 32;
        srcnn_context->conv2.size = 1;
        CHECK_ALLOCATION(allocate_copy_conv_data(&srcnn_context->conv2, conv2_kernel, conv2_biases), )

        srcnn_context->conv3.input_channels = 32;
        srcnn_context->conv3.output_channels = 1;
        srcnn_context->conv3.size = 5;
        CHECK_ALLOCATION(allocate_copy_conv_data(&srcnn_context->conv3, conv3_kernel, conv3_biases), )
    }
    else if (avio_check(srcnn_context->config_file_path, AVIO_FLAG_READ) > 0){
        if (avio_open(&config_file_context, srcnn_context->config_file_path, AVIO_FLAG_READ) < 0){
            av_log(context, AV_LOG_ERROR, "failed to open configuration file\n");
            return AVERROR(EIO);
        }

        file_size = avio_size(config_file_context);

        /// Create convolution kernels and read weights from file
        srcnn_context->conv1.input_channels = 1;
        srcnn_context->conv1.size = (int32_t)avio_rl32(config_file_context);
        srcnn_context->conv1.output_channels = (int32_t)avio_rl32(config_file_context);
        srcnn_size = 8 + (srcnn_context->conv1.output_channels * srcnn_context->conv1.size *
                          srcnn_context->conv1.size * srcnn_context->conv1.input_channels +
                          srcnn_context->conv1.output_channels << 3);
        CHECK_FILE_SIZE(file_size, srcnn_size, config_file_context)
        CHECK_ALLOCATION(allocate_read_conv_data(&srcnn_context->conv1, config_file_context), avio_closep(&config_file_context))

        srcnn_context->conv2.input_channels = (int32_t)avio_rl32(config_file_context);
        srcnn_context->conv2.size = (int32_t)avio_rl32(config_file_context);
        srcnn_context->conv2.output_channels = (int32_t)avio_rl32(config_file_context);
        srcnn_size += 12 + (srcnn_context->conv2.output_channels * srcnn_context->conv2.size *
                            srcnn_context->conv2.size * srcnn_context->conv2.input_channels +
                            srcnn_context->conv2.output_channels << 3);
        CHECK_FILE_SIZE(file_size, srcnn_size, config_file_context)
        CHECK_ALLOCATION(allocate_read_conv_data(&srcnn_context->conv2, config_file_context), avio_closep(&config_file_context))

        srcnn_context->conv3.input_channels = (int32_t)avio_rl32(config_file_context);
        srcnn_context->conv3.size = (int32_t)avio_rl32(config_file_context);
        srcnn_context->conv3.output_channels = 1;
        srcnn_size += 8 + (srcnn_context->conv3.output_channels * srcnn_context->conv3.size *
                           srcnn_context->conv3.size * srcnn_context->conv3.input_channels
                           + srcnn_context->conv3.output_channels << 3);
        if (file_size != srcnn_size){
            av_log(context, AV_LOG_ERROR, "error reading configuration file\n");
            avio_closep(&config_file_context);
            return AVERROR(EIO);
        }
        CHECK_ALLOCATION(allocate_read_conv_data(&srcnn_context->conv3, config_file_context), avio_closep(&config_file_context))

        avio_closep(&config_file_context);
    }
    else{
        av_log(context, AV_LOG_ERROR, "specified configuration file does not exist or not readable\n");
        return AVERROR(EIO);
    }

    return 0;
}

static int query_formats(AVFilterContext* context)
{
    const enum AVPixelFormat pixel_formats[] = {AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P,
                                                AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_GRAY8,
                                                AV_PIX_FMT_NONE};
    AVFilterFormats *formats_list;

    formats_list = ff_make_format_list(pixel_formats);
    if (!formats_list){
        av_log(context, AV_LOG_ERROR, "could not create formats list\n");
        return AVERROR(ENOMEM);
    }
    return ff_set_common_formats(context, formats_list);
}

static int config_props(AVFilterLink* inlink)
{
    AVFilterContext *context = inlink->dst;
    SRCNNContext *srcnn_context = context->priv;
    int min_dim;

    /// Check if input data width or height is too low
    min_dim = FFMIN(inlink->w, inlink->h);
    if (min_dim <= srcnn_context->conv1.size >> 1 || min_dim <= srcnn_context->conv2.size >> 1 || min_dim <= srcnn_context->conv3.size >> 1){
        av_log(context, AV_LOG_ERROR, "input width or height is too low\n");
        return AVERROR(EIO);
    }

    /// Allocate network buffers
    srcnn_context->input_output_buf = av_malloc(inlink->h * inlink->w * sizeof(double));
    srcnn_context->conv1_buf = av_malloc(inlink->h * inlink->w * srcnn_context->conv1.output_channels * sizeof(double));
    srcnn_context->conv2_buf = av_malloc(inlink->h * inlink->w * srcnn_context->conv2.output_channels * sizeof(double));

    if (!srcnn_context->input_output_buf || !srcnn_context->conv1_buf || !srcnn_context->conv2_buf){
        av_log(context, AV_LOG_ERROR, "could not allocate memory for srcnn buffers\n");
        return AVERROR(ENOMEM);
    }

    return 0;
}

typedef struct ThreadData{
    uint8_t* out;
    int out_linesize, height, width;
} ThreadData;

typedef struct ConvThreadData
{
    const Convolution* conv;
    const double* input;
    double* output;
    int height, width;
} ConvThreadData;

/// Convert uint8 data to double and scale it to use in network
static int uint8_to_double(AVFilterContext* context, void* arg, int jobnr, int nb_jobs)
{
    SRCNNContext* srcnn_context = context->priv;
    const ThreadData* td = arg;
    const int slice_start = (td->height *  jobnr     ) / nb_jobs;
    const int slice_end   = (td->height * (jobnr + 1)) / nb_jobs;
    const uint8_t* src = td->out + slice_start * td->out_linesize;
    double* dst = srcnn_context->input_output_buf + slice_start * td->width;
    int y, x;

    for (y = slice_start; y < slice_end; ++y){
        for (x = 0; x < td->width; ++x){
            dst[x] = (double)src[x] / 255.0;
        }
        src += td->out_linesize;
        dst += td->width;
    }

    return 0;
}

/// Convert double data from network to uint8 and scale it to output as filter result
static int double_to_uint8(AVFilterContext* context, void* arg, int jobnr, int nb_jobs)
{
    SRCNNContext* srcnn_context = context->priv;
    const ThreadData* td = arg;
    const int slice_start = (td->height *  jobnr     ) / nb_jobs;
    const int slice_end   = (td->height * (jobnr + 1)) / nb_jobs;
    const double* src = srcnn_context->input_output_buf + slice_start * td->width;
    uint8_t* dst = td->out + slice_start * td->out_linesize;
    int y, x;

    for (y = slice_start; y < slice_end; ++y){
        for (x = 0; x < td->width; ++x){
            dst[x] = (uint8_t)(255.0 * FFMIN(src[x], 1.0));
        }
        src += td->width;
        dst += td->out_linesize;
    }

    return 0;
}

#define CLAMP_TO_EDGE(x, w) ((x) < 0 ? 0 : ((x) >= (w) ? (w - 1) : (x)))

static int convolve(AVFilterContext* context, void* arg, int jobnr, int nb_jobs)
{
    const ConvThreadData* td = arg;
    const int slice_start = (td->height *  jobnr     ) / nb_jobs;
    const int slice_end   = (td->height * (jobnr + 1)) / nb_jobs;
    const double* src = td->input;
    double* dst = td->output + slice_start * td->width * td->conv->output_channels;
    int y, x;
    int32_t n_filter, ch, kernel_y, kernel_x;
    int32_t radius = td->conv->size >> 1;
    int src_linesize = td->width * td->conv->input_channels;
    int filter_linesize = td->conv->size * td->conv->input_channels;
    int filter_size = td->conv->size * filter_linesize;

    for (y = slice_start; y < slice_end; ++y){
        for (x = 0; x < td->width; ++x){
            for (n_filter = 0; n_filter < td->conv->output_channels; ++n_filter){
                dst[n_filter] = td->conv->biases[n_filter];
                for (ch = 0; ch < td->conv->input_channels; ++ch){
                    for (kernel_y = 0; kernel_y < td->conv->size; ++kernel_y){
                        for (kernel_x = 0; kernel_x < td->conv->size; ++kernel_x){
                            dst[n_filter] += src[CLAMP_TO_EDGE(y + kernel_y - radius, td->height) * src_linesize +
                                                 CLAMP_TO_EDGE(x + kernel_x - radius, td->width) * td->conv->input_channels + ch] *
                                             td->conv->kernel[n_filter * filter_size + kernel_y * filter_linesize +
                                                              kernel_x * td->conv->input_channels + ch];
                        }
                    }
                }
                dst[n_filter] = FFMAX(dst[n_filter], 0.0);
            }
            dst += td->conv->output_channels;
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink* inlink, AVFrame* in)
{
    AVFilterContext* context = inlink->dst;
    SRCNNContext* srcnn_context = context->priv;
    AVFilterLink* outlink = context->outputs[0];
    AVFrame* out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    ThreadData td;
    ConvThreadData ctd;
    int nb_threads;

    if (!out){
        av_log(context, AV_LOG_ERROR, "could not allocate memory for output frame\n");
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);
    av_frame_copy(out, in);
    av_frame_free(&in);
    td.out = out->data[0];
    td.out_linesize = out->linesize[0];
    td.height = ctd.height = out->height;
    td.width = ctd.width = out->width;

    nb_threads = ff_filter_get_nb_threads(context);
    context->internal->execute(context, uint8_to_double, &td, NULL, FFMIN(td.height, nb_threads));
    ctd.conv = &srcnn_context->conv1;
    ctd.input = srcnn_context->input_output_buf;
    ctd.output = srcnn_context->conv1_buf;
    context->internal->execute(context, convolve, &ctd, NULL, FFMIN(ctd.height, nb_threads));
    ctd.conv = &srcnn_context->conv2;
    ctd.input = srcnn_context->conv1_buf;
    ctd.output = srcnn_context->conv2_buf;
    context->internal->execute(context, convolve, &ctd, NULL, FFMIN(ctd.height, nb_threads));
    ctd.conv = &srcnn_context->conv3;
    ctd.input = srcnn_context->conv2_buf;
    ctd.output = srcnn_context->input_output_buf;
    context->internal->execute(context, convolve, &ctd, NULL, FFMIN(ctd.height, nb_threads));
    context->internal->execute(context, double_to_uint8, &td, NULL, FFMIN(td.height, nb_threads));

    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext* context)
{
    SRCNNContext* srcnn_context = context->priv;

    /// Free convolution data
    av_freep(&srcnn_context->conv1.kernel);
    av_freep(&srcnn_context->conv1.biases);
    av_freep(&srcnn_context->conv2.kernel);
    av_freep(&srcnn_context->conv2.biases);
    av_freep(&srcnn_context->conv3.kernel);
    av_freep(&srcnn_context->conv3.kernel);

    /// Free network buffers
    av_freep(&srcnn_context->input_output_buf);
    av_freep(&srcnn_context->conv1_buf);
    av_freep(&srcnn_context->conv2_buf);
}

static const AVFilterPad srcnn_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad srcnn_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_srcnn = {
    .name          = "srcnn",
    .description   = NULL_IF_CONFIG_SMALL("Apply super resolution convolutional neural network to the input. Use bicubic upsamping with corresponding scaling factor before."),
    .priv_size     = sizeof(SRCNNContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = srcnn_inputs,
    .outputs       = srcnn_outputs,
    .priv_class    = &srcnn_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
