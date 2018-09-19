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
#include <float.h>

#include "libavutil/avassert.h"
#include "libavutil/bprint.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "internal.h"
#include "opencl.h"
#include "opencl_source.h"
#include "video.h"
#include "colorspace.h"

// TODO:
// - separate peak-detection from tone-mapping kernel to solve
//    one-frame-delay issue.
// - import colorspace matrix generation from vf_colorspace.c
// - more format support

#define DETECTION_FRAMES 63

enum TonemapAlgorithm {
    TONEMAP_NONE,
    TONEMAP_LINEAR,
    TONEMAP_GAMMA,
    TONEMAP_CLIP,
    TONEMAP_REINHARD,
    TONEMAP_HABLE,
    TONEMAP_MOBIUS,
    TONEMAP_MAX,
};

typedef struct TonemapOpenCLContext {
    OpenCLFilterContext ocf;

    enum AVColorSpace colorspace, colorspace_in, colorspace_out;
    enum AVColorTransferCharacteristic trc, trc_in, trc_out;
    enum AVColorPrimaries primaries, primaries_in, primaries_out;
    enum AVColorRange range, range_in, range_out;
    enum AVChromaLocation chroma_loc;

    enum TonemapAlgorithm tonemap;
    enum AVPixelFormat    format;
    double                peak;
    double                param;
    double                desat_param;
    double                target_peak;
    double                scene_threshold;
    int                   initialised;
    cl_kernel             kernel;
    cl_command_queue      command_queue;
    cl_mem                util_mem;
} TonemapOpenCLContext;

static const char *yuv_coff[AVCOL_SPC_NB] = {
    [AVCOL_SPC_BT709] = "rgb2yuv_bt709",
    [AVCOL_SPC_BT2020_NCL] = "rgb2yuv_bt2020",
};

static const char *rgb_coff[AVCOL_SPC_NB] = {
    [AVCOL_SPC_BT709] = "yuv2rgb_bt709",
    [AVCOL_SPC_BT2020_NCL] = "yuv2rgb_bt2020",
};

static const char *linearize_funcs[AVCOL_TRC_NB] = {
    [AVCOL_TRC_SMPTE2084] = "eotf_st2084",
    [AVCOL_TRC_ARIB_STD_B67] = "inverse_oetf_hlg",
};

static const char *delinearize_funcs[AVCOL_TRC_NB] = {
    [AVCOL_TRC_BT709]     = "inverse_eotf_bt1886",
    [AVCOL_TRC_BT2020_10] = "inverse_eotf_bt1886",
};

static const struct LumaCoefficients luma_coefficients[AVCOL_SPC_NB] = {
    [AVCOL_SPC_BT709]      = { 0.2126, 0.7152, 0.0722 },
    [AVCOL_SPC_BT2020_NCL] = { 0.2627, 0.6780, 0.0593 },
};

static struct PrimaryCoefficients primaries_table[AVCOL_PRI_NB] = {
    [AVCOL_PRI_BT709]  = { 0.640, 0.330, 0.300, 0.600, 0.150, 0.060 },
    [AVCOL_PRI_BT2020] = { 0.708, 0.292, 0.170, 0.797, 0.131, 0.046 },
};

static struct WhitepointCoefficients whitepoint_table[AVCOL_PRI_NB] = {
    [AVCOL_PRI_BT709]  = { 0.3127, 0.3290 },
    [AVCOL_PRI_BT2020] = { 0.3127, 0.3290 },
};

static const char *tonemap_func[TONEMAP_MAX] = {
    [TONEMAP_NONE]     = "direct",
    [TONEMAP_LINEAR]   = "linear",
    [TONEMAP_GAMMA]    = "gamma",
    [TONEMAP_CLIP]     = "clip",
    [TONEMAP_REINHARD] = "reinhard",
    [TONEMAP_HABLE]    = "hable",
    [TONEMAP_MOBIUS]   = "mobius",
};

static void get_rgb2rgb_matrix(enum AVColorPrimaries in, enum AVColorPrimaries out,
                               double rgb2rgb[3][3]) {
    double rgb2xyz[3][3], xyz2rgb[3][3];

    ff_fill_rgb2xyz_table(&primaries_table[out], &whitepoint_table[out], rgb2xyz);
    ff_matrix_invert_3x3(rgb2xyz, xyz2rgb);
    ff_fill_rgb2xyz_table(&primaries_table[in], &whitepoint_table[in], rgb2xyz);
    ff_matrix_mul_3x3(rgb2rgb, rgb2xyz, xyz2rgb);
}

#define OPENCL_SOURCE_NB 3
// Average light level for SDR signals. This is equal to a signal level of 0.5
// under a typical presentation gamma of about 2.0.
static const float sdr_avg = 0.25f;

static int tonemap_opencl_init(AVFilterContext *avctx)
{
    TonemapOpenCLContext *ctx = avctx->priv;
    int rgb2rgb_passthrough = 1;
    double rgb2rgb[3][3];
    struct LumaCoefficients luma_src, luma_dst;
    cl_int cle;
    int err;
    AVBPrint header;
    const char *opencl_sources[OPENCL_SOURCE_NB];

    av_bprint_init(&header, 1024, AV_BPRINT_SIZE_AUTOMATIC);

    switch(ctx->tonemap) {
    case TONEMAP_GAMMA:
        if (isnan(ctx->param))
            ctx->param = 1.8f;
        break;
    case TONEMAP_REINHARD:
        if (!isnan(ctx->param))
            ctx->param = (1.0f - ctx->param) / ctx->param;
        break;
    case TONEMAP_MOBIUS:
        if (isnan(ctx->param))
            ctx->param = 0.3f;
        break;
    }

    if (isnan(ctx->param))
        ctx->param = 1.0f;

    // SDR peak is 1.0f
    ctx->target_peak = 1.0f;
    av_log(ctx, AV_LOG_DEBUG, "tone mapping transfer from %s to %s\n",
           av_color_transfer_name(ctx->trc_in),
           av_color_transfer_name(ctx->trc_out));
    av_log(ctx, AV_LOG_DEBUG, "mapping colorspace from %s to %s\n",
           av_color_space_name(ctx->colorspace_in),
           av_color_space_name(ctx->colorspace_out));
    av_log(ctx, AV_LOG_DEBUG, "mapping primaries from %s to %s\n",
           av_color_primaries_name(ctx->primaries_in),
           av_color_primaries_name(ctx->primaries_out));
    av_log(ctx, AV_LOG_DEBUG, "mapping range from %s to %s\n",
           av_color_range_name(ctx->range_in),
           av_color_range_name(ctx->range_out));
    // checking valid value just because of limited implementaion
    // please remove when more functionalities are implemented
    av_assert0(ctx->trc_out == AVCOL_TRC_BT709 ||
               ctx->trc_out == AVCOL_TRC_BT2020_10);
    av_assert0(ctx->trc_in == AVCOL_TRC_SMPTE2084||
               ctx->trc_in == AVCOL_TRC_ARIB_STD_B67);
    av_assert0(ctx->colorspace_in == AVCOL_SPC_BT2020_NCL ||
               ctx->colorspace_in == AVCOL_SPC_BT709);
    av_assert0(ctx->primaries_in == AVCOL_PRI_BT2020 ||
               ctx->primaries_in == AVCOL_PRI_BT709);

    av_bprintf(&header, "__constant const float tone_param = %.4ff;\n",
               ctx->param);
    av_bprintf(&header, "__constant const float desat_param = %.4ff;\n",
               ctx->desat_param);
    av_bprintf(&header, "__constant const float target_peak = %.4ff;\n",
               ctx->target_peak);
    av_bprintf(&header, "__constant const float sdr_avg = %.4ff;\n", sdr_avg);
    av_bprintf(&header, "__constant const float scene_threshold = %.4ff;\n",
               ctx->scene_threshold);
    av_bprintf(&header, "#define TONE_FUNC %s\n", tonemap_func[ctx->tonemap]);
    av_bprintf(&header, "#define DETECTION_FRAMES %d\n", DETECTION_FRAMES);

    if (ctx->primaries_out != ctx->primaries_in) {
        get_rgb2rgb_matrix(ctx->primaries_in, ctx->primaries_out, rgb2rgb);
        rgb2rgb_passthrough = 0;
    }
    if (ctx->range_in == AVCOL_RANGE_JPEG)
        av_bprintf(&header, "#define FULL_RANGE_IN\n");

    if (ctx->range_out == AVCOL_RANGE_JPEG)
        av_bprintf(&header, "#define FULL_RANGE_OUT\n");

    av_bprintf(&header, "#define chroma_loc %d\n", (int)ctx->chroma_loc);

    if (rgb2rgb_passthrough)
        av_bprintf(&header, "#define RGB2RGB_PASSTHROUGH\n");
    else {
        av_bprintf(&header, "__constant float rgb2rgb[9] = {\n");
        av_bprintf(&header, "    %.4ff, %.4ff, %.4ff,\n",
                   rgb2rgb[0][0], rgb2rgb[0][1], rgb2rgb[0][2]);
        av_bprintf(&header, "    %.4ff, %.4ff, %.4ff,\n",
                   rgb2rgb[1][0], rgb2rgb[1][1], rgb2rgb[1][2]);
        av_bprintf(&header, "    %.4ff, %.4ff, %.4ff};\n",
                   rgb2rgb[2][0], rgb2rgb[2][1], rgb2rgb[2][2]);
    }

    av_bprintf(&header, "#define rgb_matrix %s\n",
               rgb_coff[ctx->colorspace_in]);
    av_bprintf(&header, "#define yuv_matrix %s\n",
               yuv_coff[ctx->colorspace_out]);

    luma_src = luma_coefficients[ctx->colorspace_in];
    luma_dst = luma_coefficients[ctx->colorspace_out];
    av_bprintf(&header, "constant float3 luma_src = {%.4ff, %.4ff, %.4ff};\n",
               luma_src.cr, luma_src.cg, luma_src.cb);
    av_bprintf(&header, "constant float3 luma_dst = {%.4ff, %.4ff, %.4ff};\n",
               luma_dst.cr, luma_dst.cg, luma_dst.cb);

    av_bprintf(&header, "#define linearize %s\n", linearize_funcs[ctx->trc_in]);
    av_bprintf(&header, "#define delinearize %s\n",
               delinearize_funcs[ctx->trc_out]);

    if (ctx->trc_in == AVCOL_TRC_ARIB_STD_B67)
        av_bprintf(&header, "#define ootf_impl ootf_hlg\n");

    if (ctx->trc_out == AVCOL_TRC_ARIB_STD_B67)
        av_bprintf(&header, "#define inverse_ootf_impl inverse_ootf_hlg\n");

    av_log(avctx, AV_LOG_DEBUG, "Generated OpenCL header:\n%s\n", header.str);
    opencl_sources[0] = header.str;
    opencl_sources[1] = ff_opencl_source_tonemap;
    opencl_sources[2] = ff_opencl_source_colorspace_common;
    err = ff_opencl_filter_load_program(avctx, opencl_sources, OPENCL_SOURCE_NB);

    av_bprint_finalize(&header, NULL);
    if (err < 0)
        goto fail;

    ctx->command_queue = clCreateCommandQueue(ctx->ocf.hwctx->context,
                                              ctx->ocf.hwctx->device_id,
                                              0, &cle);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create OpenCL "
                     "command queue %d.\n", cle);

    ctx->kernel = clCreateKernel(ctx->ocf.program, "tonemap", &cle);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create kernel %d.\n", cle);

    ctx->util_mem =
        clCreateBuffer(ctx->ocf.hwctx->context, 0,
                       (2 * DETECTION_FRAMES + 7) * sizeof(unsigned),
                       NULL, &cle);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create util buffer: %d.\n", cle);

    ctx->initialised = 1;
    return 0;

fail:
    if (ctx->util_mem)
        clReleaseMemObject(ctx->util_mem);
    if (ctx->command_queue)
        clReleaseCommandQueue(ctx->command_queue);
    if (ctx->kernel)
        clReleaseKernel(ctx->kernel);
    return err;
}

static int tonemap_opencl_config_output(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    TonemapOpenCLContext *s = avctx->priv;
    int ret;
    if (s->format == AV_PIX_FMT_NONE)
        av_log(avctx, AV_LOG_WARNING, "format not set, use default format NV12\n");
    else {
      if (s->format != AV_PIX_FMT_P010 &&
          s->format != AV_PIX_FMT_NV12) {
        av_log(avctx, AV_LOG_ERROR, "unsupported output format,"
               "only p010/nv12 supported now\n");
        return AVERROR(EINVAL);
      }
    }

    s->ocf.output_format = s->format == AV_PIX_FMT_NONE ? AV_PIX_FMT_NV12 : s->format;
    ret = ff_opencl_filter_config_output(outlink);
    if (ret < 0)
        return ret;

    return 0;
}

static int launch_kernel(AVFilterContext *avctx, cl_kernel kernel,
                         AVFrame *output, AVFrame *input, float peak) {
    TonemapOpenCLContext *ctx = avctx->priv;
    int err = AVERROR(ENOSYS);
    size_t global_work[2];
    size_t local_work[2];
    cl_int cle;

    CL_SET_KERNEL_ARG(kernel, 0, cl_mem, &output->data[0]);
    CL_SET_KERNEL_ARG(kernel, 1, cl_mem, &input->data[0]);
    CL_SET_KERNEL_ARG(kernel, 2, cl_mem, &output->data[1]);
    CL_SET_KERNEL_ARG(kernel, 3, cl_mem, &input->data[1]);
    CL_SET_KERNEL_ARG(kernel, 4, cl_mem, &ctx->util_mem);
    CL_SET_KERNEL_ARG(kernel, 5, cl_float, &peak);

    local_work[0]  = 16;
    local_work[1]  = 16;
    // Note the work size based on uv plane, as we process a 2x2 quad in one workitem
    err = ff_opencl_filter_work_size_from_image(avctx, global_work, output,
                                                1, 16);
    if (err < 0)
        return err;

    cle = clEnqueueNDRangeKernel(ctx->command_queue, kernel, 2, NULL,
                                 global_work, local_work,
                                 0, NULL, NULL);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to enqueue kernel: %d.\n", cle);
    return 0;
fail:
    return err;
}

static int tonemap_opencl_filter_frame(AVFilterLink *inlink, AVFrame *input)
{
    AVFilterContext    *avctx = inlink->dst;
    AVFilterLink     *outlink = avctx->outputs[0];
    TonemapOpenCLContext *ctx = avctx->priv;
    AVFrame *output = NULL;
    cl_int cle;
    int err;
    double peak = ctx->peak;

    AVHWFramesContext *input_frames_ctx =
        (AVHWFramesContext*)input->hw_frames_ctx->data;

    av_log(ctx, AV_LOG_DEBUG, "Filter input: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(input->format),
           input->width, input->height, input->pts);

    if (!input->hw_frames_ctx)
        return AVERROR(EINVAL);

    output = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!output) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    err = av_frame_copy_props(output, input);
    if (err < 0)
        goto fail;

    if (!peak)
        peak = ff_determine_signal_peak(input);

    if (ctx->trc != -1)
        output->color_trc = ctx->trc;
    if (ctx->primaries != -1)
        output->color_primaries = ctx->primaries;
    if (ctx->colorspace != -1)
        output->colorspace = ctx->colorspace;
    if (ctx->range != -1)
        output->color_range = ctx->range;

    ctx->trc_in = input->color_trc;
    ctx->trc_out = output->color_trc;
    ctx->colorspace_in = input->colorspace;
    ctx->colorspace_out = output->colorspace;
    ctx->primaries_in = input->color_primaries;
    ctx->primaries_out = output->color_primaries;
    ctx->range_in = input->color_range;
    ctx->range_out = output->color_range;
    ctx->chroma_loc = output->chroma_location;

    if (!ctx->initialised) {
        if (!(input->color_trc == AVCOL_TRC_SMPTE2084 ||
            input->color_trc == AVCOL_TRC_ARIB_STD_B67)) {
            av_log(ctx, AV_LOG_ERROR, "unsupported transfer function characteristic.\n");
            err = AVERROR(ENOSYS);
            goto fail;
        }

        if (input_frames_ctx->sw_format != AV_PIX_FMT_P010) {
            av_log(ctx, AV_LOG_ERROR, "unsupported format in tonemap_opencl.\n");
            err = AVERROR(ENOSYS);
            goto fail;
        }

        err = tonemap_opencl_init(avctx);
        if (err < 0)
            goto fail;
    }

    switch(input_frames_ctx->sw_format) {
    case AV_PIX_FMT_P010:
        err = launch_kernel(avctx, ctx->kernel, output, input, peak);
        if (err < 0) goto fail;
        break;
    default:
        err = AVERROR(ENOSYS);
        goto fail;
    }

    cle = clFinish(ctx->command_queue);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to finish command queue: %d.\n", cle);

    av_frame_free(&input);

    ff_update_hdr_metadata(output, ctx->target_peak);

    av_log(ctx, AV_LOG_DEBUG, "Tone-mapping output: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(output->format),
           output->width, output->height, output->pts);
#ifndef NDEBUG
    {
        uint32_t *ptr, *max_total_p, *avg_total_p, *frame_number_p;
        float peak_detected, avg_detected;
        unsigned map_size = (2 * DETECTION_FRAMES  + 7) * sizeof(unsigned);
        ptr = (void *)clEnqueueMapBuffer(ctx->command_queue, ctx->util_mem,
                                         CL_TRUE, CL_MAP_READ, 0, map_size,
                                         0, NULL, NULL, &cle);
        // For the layout of the util buffer, refer tonemap.cl
        if (ptr) {
            max_total_p = ptr + 2 * (DETECTION_FRAMES + 1) + 1;
            avg_total_p = max_total_p + 1;
            frame_number_p = avg_total_p + 2;
            peak_detected = (float)*max_total_p / (REFERENCE_WHITE * (*frame_number_p));
            avg_detected = (float)*avg_total_p / (REFERENCE_WHITE * (*frame_number_p));
            av_log(ctx, AV_LOG_DEBUG, "peak %f, avg %f will be used for next frame\n",
                   peak_detected, avg_detected);
            clEnqueueUnmapMemObject(ctx->command_queue, ctx->util_mem, ptr, 0,
                                    NULL, NULL);
        }
    }
#endif

    return ff_filter_frame(outlink, output);

fail:
    clFinish(ctx->command_queue);
    av_frame_free(&input);
    av_frame_free(&output);
    return err;
}

static av_cold void tonemap_opencl_uninit(AVFilterContext *avctx)
{
    TonemapOpenCLContext *ctx = avctx->priv;
    cl_int cle;

    if (ctx->util_mem)
        clReleaseMemObject(ctx->util_mem);
    if (ctx->kernel) {
        cle = clReleaseKernel(ctx->kernel);
        if (cle != CL_SUCCESS)
            av_log(avctx, AV_LOG_ERROR, "Failed to release "
                   "kernel: %d.\n", cle);
    }

    if (ctx->command_queue) {
        cle = clReleaseCommandQueue(ctx->command_queue);
        if (cle != CL_SUCCESS)
            av_log(avctx, AV_LOG_ERROR, "Failed to release "
                   "command queue: %d.\n", cle);
    }

    ff_opencl_filter_uninit(avctx);
}

#define OFFSET(x) offsetof(TonemapOpenCLContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption tonemap_opencl_options[] = {
    { "tonemap",      "tonemap algorithm selection", OFFSET(tonemap), AV_OPT_TYPE_INT, {.i64 = TONEMAP_NONE}, TONEMAP_NONE, TONEMAP_MAX - 1, FLAGS, "tonemap" },
    {     "none",     0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_NONE},              0, 0, FLAGS, "tonemap" },
    {     "linear",   0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_LINEAR},            0, 0, FLAGS, "tonemap" },
    {     "gamma",    0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_GAMMA},             0, 0, FLAGS, "tonemap" },
    {     "clip",     0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_CLIP},              0, 0, FLAGS, "tonemap" },
    {     "reinhard", 0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_REINHARD},          0, 0, FLAGS, "tonemap" },
    {     "hable",    0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_HABLE},             0, 0, FLAGS, "tonemap" },
    {     "mobius",   0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_MOBIUS},            0, 0, FLAGS, "tonemap" },
    { "transfer", "set transfer characteristic", OFFSET(trc), AV_OPT_TYPE_INT, {.i64 = AVCOL_TRC_BT709}, -1, INT_MAX, FLAGS, "transfer" },
    { "t",        "set transfer characteristic", OFFSET(trc), AV_OPT_TYPE_INT, {.i64 = AVCOL_TRC_BT709}, -1, INT_MAX, FLAGS, "transfer" },
    {     "bt709",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_BT709},         0, 0, FLAGS, "transfer" },
    {     "bt2020",           0,       0,                 AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_BT2020_10},     0, 0, FLAGS, "transfer" },
    { "matrix", "set colorspace matrix", OFFSET(colorspace), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, "matrix" },
    { "m",      "set colorspace matrix", OFFSET(colorspace), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, "matrix" },
    {     "bt709",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = AVCOL_SPC_BT709},         0, 0, FLAGS, "matrix" },
    {     "bt2020",           0,       0,                 AV_OPT_TYPE_CONST, {.i64 = AVCOL_SPC_BT2020_NCL},    0, 0, FLAGS, "matrix" },
    { "primaries", "set color primaries", OFFSET(primaries), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, "primaries" },
    { "p",         "set color primaries", OFFSET(primaries), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, "primaries" },
    {     "bt709",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = AVCOL_PRI_BT709},         0, 0, FLAGS, "primaries" },
    {     "bt2020",           0,       0,                 AV_OPT_TYPE_CONST, {.i64 = AVCOL_PRI_BT2020},        0, 0, FLAGS, "primaries" },
    { "range",         "set color range", OFFSET(range), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, "range" },
    { "r",             "set color range", OFFSET(range), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, "range" },
    {     "tv",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_MPEG},         0, 0, FLAGS, "range" },
    {     "pc",            0,       0,                 AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_JPEG},         0, 0, FLAGS, "range" },
    {     "limited",       0,       0,                 AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_MPEG},         0, 0, FLAGS, "range" },
    {     "full",          0,       0,                 AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_JPEG},         0, 0, FLAGS, "range" },
    { "format",    "output pixel format", OFFSET(format), AV_OPT_TYPE_PIXEL_FMT, {.i64 = AV_PIX_FMT_NONE}, AV_PIX_FMT_NONE, INT_MAX, FLAGS, "fmt" },
    { "peak",      "signal peak override", OFFSET(peak), AV_OPT_TYPE_DOUBLE, {.dbl = 0}, 0, DBL_MAX, FLAGS },
    { "param",     "tonemap parameter",   OFFSET(param), AV_OPT_TYPE_DOUBLE, {.dbl = NAN}, DBL_MIN, DBL_MAX, FLAGS },
    { "desat",     "desaturation parameter",   OFFSET(desat_param), AV_OPT_TYPE_DOUBLE, {.dbl = 0.5}, 0, DBL_MAX, FLAGS },
    { "threshold", "scene detection threshold",   OFFSET(scene_threshold), AV_OPT_TYPE_DOUBLE, {.dbl = 0.2}, 0, DBL_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tonemap_opencl);

static const AVFilterPad tonemap_opencl_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &tonemap_opencl_filter_frame,
        .config_props = &ff_opencl_filter_config_input,
    },
    { NULL }
};

static const AVFilterPad tonemap_opencl_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &tonemap_opencl_config_output,
    },
    { NULL }
};

AVFilter ff_vf_tonemap_opencl = {
    .name           = "tonemap_opencl",
    .description    = NULL_IF_CONFIG_SMALL("perform HDR to SDR conversion with tonemapping"),
    .priv_size      = sizeof(TonemapOpenCLContext),
    .priv_class     = &tonemap_opencl_class,
    .init           = &ff_opencl_filter_init,
    .uninit         = &tonemap_opencl_uninit,
    .query_formats  = &ff_opencl_filter_query_formats,
    .inputs         = tonemap_opencl_inputs,
    .outputs        = tonemap_opencl_outputs,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
