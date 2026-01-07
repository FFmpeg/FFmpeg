/*
 * Copyright (c) 2026 Sam Richards
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


#include <OpenColorIO/OpenColorIO.h>
#include <exception>

namespace OCIO = OCIO_NAMESPACE;

struct OCIOState {
    OCIO::ConstConfigRcPtr config;
    OCIO::ConstProcessorRcPtr processor;
    OCIO::ConstCPUProcessorRcPtr cpu;
    int channels;
};

extern "C" {

#include "formats.h"
#include "ocio_wrapper.hpp"
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libavutil/dict.h>

// Helper to map AV_PIX_FMT to OCIO BitDepth
static OCIO::BitDepth get_ocio_depth(int format)
{
    switch (format) {
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_RGBA:
        return OCIO::BIT_DEPTH_UINT8;

    case AV_PIX_FMT_RGB48:
    case AV_PIX_FMT_RGBA64:
        return OCIO::BIT_DEPTH_UINT16;

    case AV_PIX_FMT_GBRP10:
    case AV_PIX_FMT_GBRAP10:
        return OCIO::BIT_DEPTH_UINT10;

    case AV_PIX_FMT_GBRP12:
    case AV_PIX_FMT_GBRAP12:
        return OCIO::BIT_DEPTH_UINT12;

    // Note: FFmpeg treats half-float as specific types often requiring casts.
    // For this snippet we map F16 directly if your system supports it,
    // otherwise, standard float (F32) is safer.
    case AV_PIX_FMT_GBRPF16:
    case AV_PIX_FMT_GBRAPF16:
        return OCIO::BIT_DEPTH_F16;

    case AV_PIX_FMT_GBRPF32:
    case AV_PIX_FMT_GBRAPF32:
        return OCIO::BIT_DEPTH_F32;

    default:
        return OCIO::BIT_DEPTH_UNKNOWN;
    }
}

static OCIO::ConstContextRcPtr add_context_params(OCIO::ConstConfigRcPtr config, AVDictionary *params)
{

    OCIO::ConstContextRcPtr context = config->getCurrentContext();
    if (!params)
        return context;
    if (!context)
        return nullptr;

    OCIO::ContextRcPtr ctx = context->createEditableCopy();
    if (!ctx) {
        return context;
    }
    const AVDictionaryEntry *e = NULL;
    while ((e = av_dict_iterate(params, e))) {
        ctx->setStringVar(e->key, e->value);
    }
    return ctx;
}

OCIOHandle
ocio_create_output_colorspace_processor(AVFilterContext *ctx, const char *config_path,
                                        const char *input_color_space,
                                        const char *output_color_space,
                                        AVDictionary *params)
{
    try {
        OCIOState *s = new OCIOState();
        if (!config_path)
            s->config = OCIO::Config::CreateFromEnv();
        else
            s->config = OCIO::Config::CreateFromFile(config_path);

        if (!s->config || !input_color_space || !output_color_space) {
            av_log(ctx, AV_LOG_ERROR, "Error: Config or color spaces invalid.\n");
            if (!s->config) av_log(ctx, AV_LOG_ERROR, "Config is null\n");
            if (!input_color_space) av_log(ctx, AV_LOG_ERROR, "Input color space is null\n");
            if (!output_color_space) av_log(ctx, AV_LOG_ERROR, "Output color space is null\n");
            delete s;
            return nullptr;
        }

        // ColorSpace Transform: InputSpace -> OutputSpace
        OCIO::ColorSpaceTransformRcPtr cst = OCIO::ColorSpaceTransform::Create();
        cst->setSrc(input_color_space);
        cst->setDst(output_color_space);
        auto context = add_context_params(s->config, params);
        s->processor = s->config->getProcessor(context, cst, OCIO::TRANSFORM_DIR_FORWARD);

        return (OCIOHandle)s;
    } catch (OCIO::Exception &e) {
        av_log(ctx, AV_LOG_ERROR, "OCIO Filter: Error in create_output_colorspace_processor: %s\n", e.what());
        return nullptr;
    } catch (...) {
        av_log(ctx, AV_LOG_ERROR, "OCIO Filter: Unknown Error in create_output_colorspace_processor\n");
        return nullptr;
    }
}

OCIOHandle ocio_create_display_view_processor(AVFilterContext *ctx,
                                              const char *config_path,
                                              const char *input_color_space,
                                              const char *display,
                                              const char *view, int inverse,
                                              AVDictionary *params)
{
    try {

        OCIOState *s = new OCIOState();
        if (!config_path)
            s->config = OCIO::Config::CreateFromEnv();
        else
            s->config = OCIO::Config::CreateFromFile(config_path);

        if (!s->config || !input_color_space || !display || !view) {
            av_log(ctx, AV_LOG_ERROR, "Error: Config or arguments invalid.\n");
            if (!s->config) av_log(ctx, AV_LOG_ERROR, "Config is null\n");
            if (!input_color_space) av_log(ctx, AV_LOG_ERROR, "Input color space is null\n");
            if (!display) av_log(ctx, AV_LOG_ERROR, "Display is null\n");
            if (!view) av_log(ctx, AV_LOG_ERROR, "View is null\n");
            delete s;
            return nullptr;
        }

        // Display/View Transform: InputSpace -> Display/View
        OCIO::DisplayViewTransformRcPtr dv = OCIO::DisplayViewTransform::Create();
        dv->setSrc(input_color_space);
        dv->setDisplay(display);
        dv->setView(view);
        OCIO::TransformDirection direction = OCIO::TRANSFORM_DIR_FORWARD;
        if (inverse)
            direction = OCIO::TRANSFORM_DIR_INVERSE;
        OCIO::ConstContextRcPtr context = add_context_params(s->config, params);
        s->processor = s->config->getProcessor(context, dv, direction);

        return (OCIOHandle)s;
    } catch (OCIO::Exception &e) {
        av_log(ctx, AV_LOG_ERROR, "OCIO Error in create_display_view_processor: %s\n", e.what());
        return nullptr;
    } catch (...) {
        av_log(ctx, AV_LOG_ERROR, "Unknown Error in create_display_view_processor\n");
        return nullptr;
    }
}

OCIOHandle ocio_create_file_transform_processor(AVFilterContext *ctx,
                                                const char *file_transform,
                                                int inverse)
{
    try {
        if (!file_transform) {
            av_log(ctx, AV_LOG_ERROR, "File transform is null\n");
            return nullptr;
        }
        OCIOState *s = new OCIOState();

        // File Transform: InputSpace -> FileTransform -> OutputSpace
        OCIO::FileTransformRcPtr ft = OCIO::FileTransform::Create();
        ft->setSrc(file_transform);
        OCIO::TransformDirection direction = OCIO::TRANSFORM_DIR_FORWARD;
        if (inverse)
            direction = OCIO::TRANSFORM_DIR_INVERSE;
        s->config = OCIO::Config::Create();
        s->processor = s->config->getProcessor(ft, direction);

        return (OCIOHandle)s;
    } catch (OCIO::Exception &e) {
        av_log(ctx, AV_LOG_ERROR, "OCIO Error in create_file_transform_processor: %s\n", e.what());
        return nullptr;
    } catch (...) {
        av_log(ctx, AV_LOG_ERROR, "Unknown Error in create_file_transform_processor\n");
        return nullptr;
    }
}

// In ocio_wrapper.cpp
int ocio_finalize_processor(AVFilterContext *ctx, OCIOHandle handle, int input_format,
                            int output_format)
{
    try {
        OCIOState *s = (OCIOState *)handle;
        if (!s || !s->processor)
            return -1;

        s->cpu = s->processor->getOptimizedCPUProcessor(
        get_ocio_depth(input_format), get_ocio_depth(output_format),
        OCIO::OPTIMIZATION_DEFAULT);

        return 0;
    } catch (OCIO::Exception &e) {
        av_log(ctx, AV_LOG_ERROR, "OCIO error: %s\n", e.what());
        return -1;
    } catch (...) {
        av_log(ctx, AV_LOG_ERROR, "Unknown error in ocio_finalize_processor\n");
        return -1;
    }
}

static OCIO::ImageDesc *AVFrame2ImageDescSlice(AVFrame *frame, int y_start,
                                               int height)
{
    OCIO::BitDepth ocio_bitdepth = get_ocio_depth(frame->format);
    if (ocio_bitdepth == OCIO::BIT_DEPTH_UNKNOWN) {
        throw std::runtime_error("Unsupported pixel format for OCIO processing");
    }

    int stridex = frame->linesize[0];
    const AVPixFmtDescriptor *desc =
        av_pix_fmt_desc_get((enum AVPixelFormat)frame->format);
    if (!desc) {
        throw std::runtime_error("Invalid pixel format descriptor");
    }

    bool is_planar = desc && (desc->flags & AV_PIX_FMT_FLAG_PLANAR);

    if (is_planar) {
        // For planar, we need to offset each plane
        uint8_t *red = frame->data[2] + y_start * frame->linesize[2];
        uint8_t *green = frame->data[0] + y_start * frame->linesize[0];
        uint8_t *blue = frame->data[1] + y_start * frame->linesize[1];
        uint8_t *alpha = (desc->nb_components == 4)
                             ? (frame->data[3] + y_start * frame->linesize[3])
                             : nullptr;

        return new OCIO::PlanarImageDesc(
            (void *)red, (void *)green, (void *)blue, (void *)alpha, frame->width,
            height, ocio_bitdepth, desc->comp[0].step, stridex);
    }

    uint8_t *data = frame->data[0] + y_start * frame->linesize[0];
    // Note we are assuming that these are RGB or RGBA channel ordering.
    // And are also likely to be integer.
    return new OCIO::PackedImageDesc(
        (void *)data, frame->width, height, desc->nb_components, ocio_bitdepth,
        desc->comp[0].depth / 8, desc->comp[0].step, frame->linesize[0]);
}

int ocio_apply(AVFilterContext *ctx, OCIOHandle handle, AVFrame *input_frame, AVFrame *output_frame,
               int y_start, int height)
{
    OCIOState *s = (OCIOState *)handle;
    if (!s || !s->cpu)
        return -1;

    try {
        if (input_frame == output_frame) {
            OCIO::ImageDesc *imgDesc = AVFrame2ImageDescSlice(input_frame, y_start, height);
            s->cpu->apply(*imgDesc);
            delete imgDesc;
            return 0;
        }

        OCIO::ImageDesc *input = AVFrame2ImageDescSlice(input_frame, y_start, height);
        OCIO::ImageDesc *output = AVFrame2ImageDescSlice(output_frame, y_start, height);
        s->cpu->apply(*input, *output);

        delete input;
        delete output;
        return 0;
    } catch (const OCIO::Exception &ex) {
        av_log(ctx, AV_LOG_ERROR, "OCIO error: %s\n", ex.what());
        return -2; // or another error code
    } catch (const std::exception &ex) {
        av_log(ctx, AV_LOG_ERROR, "OCIO error: Standard exception: %s\n", ex.what());
        return -3;
    } catch (...) {
        av_log(ctx, AV_LOG_ERROR, "OCIO error: Unknown error in OCIO processing.\n");
        return -4;
    }
}

void ocio_destroy_processor(AVFilterContext *ctx, OCIOHandle handle)
{
    if (!handle)
        return;
    delete (OCIOState *)handle;
}

} // extern "C"
