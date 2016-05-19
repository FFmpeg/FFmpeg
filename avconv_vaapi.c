/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#include <fcntl.h>
#include <unistd.h>

#include <va/va.h>
#if HAVE_VAAPI_X11
#   include <va/va_x11.h>
#endif
#if HAVE_VAAPI_DRM
#   include <va/va_drm.h>
#endif

#include "libavutil/avassert.h"
#include "libavutil/avconfig.h"
#include "libavutil/buffer.h"
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_vaapi.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"

#include "libavcodec/vaapi.h"

#include "avconv.h"


static AVClass vaapi_class = {
    .class_name = "vaapi",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

#define DEFAULT_SURFACES 20

typedef struct VAAPIDecoderContext {
    const AVClass *class;

    AVBufferRef       *device_ref;
    AVHWDeviceContext *device;
    AVBufferRef       *frames_ref;
    AVHWFramesContext *frames;

    VAProfile    va_profile;
    VAEntrypoint va_entrypoint;
    VAConfigID   va_config;
    VAContextID  va_context;

    enum AVPixelFormat decode_format;
    int decode_width;
    int decode_height;
    int decode_surfaces;

    // The output need not have the same format, width and height as the
    // decoded frames - the copy for non-direct-mapped access is actually
    // a whole vpp instance which can do arbitrary scaling and format
    // conversion.
    enum AVPixelFormat output_format;

    struct vaapi_context decoder_vaapi_context;
} VAAPIDecoderContext;


static int vaapi_get_buffer(AVCodecContext *avctx, AVFrame *frame, int flags)
{
    InputStream *ist = avctx->opaque;
    VAAPIDecoderContext *ctx = ist->hwaccel_ctx;
    int err;

    err = av_hwframe_get_buffer(ctx->frames_ref, frame, 0);
    if (err < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate decoder surface.\n");
    } else {
        av_log(ctx, AV_LOG_DEBUG, "Decoder given surface %#x.\n",
               (unsigned int)(uintptr_t)frame->data[3]);
    }
    return err;
}

static int vaapi_retrieve_data(AVCodecContext *avctx, AVFrame *input)
{
    InputStream *ist = avctx->opaque;
    VAAPIDecoderContext *ctx = ist->hwaccel_ctx;
    AVFrame *output = 0;
    int err;

    av_assert0(input->format == AV_PIX_FMT_VAAPI);

    if (ctx->output_format == AV_PIX_FMT_VAAPI) {
        // Nothing to do.
        return 0;
    }

    av_log(ctx, AV_LOG_DEBUG, "Retrieve data from surface %#x.\n",
           (unsigned int)(uintptr_t)input->data[3]);

    output = av_frame_alloc();
    if (!output)
        return AVERROR(ENOMEM);

    output->format = ctx->output_format;

    err = av_hwframe_transfer_data(output, input, 0);
    if (err < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to transfer data to "
               "output frame: %d.\n", err);
        goto fail;
    }

    err = av_frame_copy_props(output, input);
    if (err < 0) {
        av_frame_unref(output);
        goto fail;
    }

    av_frame_unref(input);
    av_frame_move_ref(input, output);
    av_frame_free(&output);

    return 0;

fail:
    if (output)
        av_frame_free(&output);
    return err;
}


static const struct {
    enum AVCodecID codec_id;
    int codec_profile;
    VAProfile va_profile;
} vaapi_profile_map[] = {
#define MAP(c, p, v) { AV_CODEC_ID_ ## c, FF_PROFILE_ ## p, VAProfile ## v }
    MAP(MPEG2VIDEO,  MPEG2_SIMPLE,    MPEG2Simple ),
    MAP(MPEG2VIDEO,  MPEG2_MAIN,      MPEG2Main   ),
    MAP(H263,        UNKNOWN,         H263Baseline),
    MAP(MPEG4,       MPEG4_SIMPLE,    MPEG4Simple ),
    MAP(MPEG4,       MPEG4_ADVANCED_SIMPLE,
                               MPEG4AdvancedSimple),
    MAP(MPEG4,       MPEG4_MAIN,      MPEG4Main   ),
    MAP(H264,        H264_CONSTRAINED_BASELINE,
                           H264ConstrainedBaseline),
    MAP(H264,        H264_BASELINE,   H264Baseline),
    MAP(H264,        H264_MAIN,       H264Main    ),
    MAP(H264,        H264_HIGH,       H264High    ),
#if VA_CHECK_VERSION(0, 37, 0)
    MAP(HEVC,        HEVC_MAIN,       HEVCMain    ),
#endif
    MAP(WMV3,        VC1_SIMPLE,      VC1Simple   ),
    MAP(WMV3,        VC1_MAIN,        VC1Main     ),
    MAP(WMV3,        VC1_COMPLEX,     VC1Advanced ),
    MAP(WMV3,        VC1_ADVANCED,    VC1Advanced ),
    MAP(VC1,         VC1_SIMPLE,      VC1Simple   ),
    MAP(VC1,         VC1_MAIN,        VC1Main     ),
    MAP(VC1,         VC1_COMPLEX,     VC1Advanced ),
    MAP(VC1,         VC1_ADVANCED,    VC1Advanced ),
#if VA_CHECK_VERSION(0, 35, 0)
    MAP(VP8,         UNKNOWN,       VP8Version0_3 ),
#endif
#if VA_CHECK_VERSION(0, 37, 1)
    MAP(VP9,         VP9_0,           VP9Profile0 ),
#endif
#undef MAP
};

static int vaapi_build_decoder_config(VAAPIDecoderContext *ctx,
                                      AVCodecContext *avctx,
                                      int fallback_allowed)
{
    AVVAAPIDeviceContext *hwctx = ctx->device->hwctx;
    AVVAAPIHWConfig *hwconfig = NULL;
    AVHWFramesConstraints *constraints = NULL;
    VAStatus vas;
    int err, i, j;
    int loglevel = fallback_allowed ? AV_LOG_VERBOSE : AV_LOG_ERROR;
    const AVCodecDescriptor *codec_desc;
    const AVPixFmtDescriptor *pix_desc;
    enum AVPixelFormat pix_fmt;
    VAProfile profile, *profile_list = NULL;
    int profile_count, exact_match, alt_profile;

    codec_desc = avcodec_descriptor_get(avctx->codec_id);
    if (!codec_desc) {
        err = AVERROR(EINVAL);
        goto fail;
    }

    profile_count = vaMaxNumProfiles(hwctx->display);
    profile_list = av_malloc(profile_count * sizeof(VAProfile));
    if (!profile_list) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    vas = vaQueryConfigProfiles(hwctx->display,
                                profile_list, &profile_count);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(ctx, loglevel, "Failed to query profiles: %d (%s).\n",
               vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail;
    }

    profile = VAProfileNone;
    exact_match = 0;

    for (i = 0; i < FF_ARRAY_ELEMS(vaapi_profile_map); i++) {
        int profile_match = 0;
        if (avctx->codec_id != vaapi_profile_map[i].codec_id)
            continue;
        if (avctx->profile == vaapi_profile_map[i].codec_profile)
            profile_match = 1;
        profile = vaapi_profile_map[i].va_profile;
        for (j = 0; j < profile_count; j++) {
            if (profile == profile_list[j]) {
                exact_match = profile_match;
                break;
            }
        }
        if (j < profile_count) {
            if (exact_match)
                break;
            alt_profile = vaapi_profile_map[i].codec_profile;
        }
    }
    av_freep(&profile_list);

    if (profile == VAProfileNone) {
        av_log(ctx, loglevel, "No VAAPI support for codec %s.\n",
               codec_desc->name);
        err = AVERROR(ENOSYS);
        goto fail;
    }
    if (!exact_match) {
        if (fallback_allowed || !hwaccel_lax_profile_check) {
            av_log(ctx, loglevel, "No VAAPI support for codec %s "
                   "profile %d.\n", codec_desc->name, avctx->profile);
            if (!fallback_allowed) {
                av_log(ctx, AV_LOG_WARNING, "If you want attempt decoding "
                       "anyway with a possibly-incompatible profile, add "
                       "the option -hwaccel_lax_profile_check.\n");
            }
            err = AVERROR(EINVAL);
            goto fail;
        } else {
            av_log(ctx, AV_LOG_WARNING, "No VAAPI support for codec %s "
                   "profile %d: trying instead with profile %d.\n",
                   codec_desc->name, avctx->profile, alt_profile);
            av_log(ctx, AV_LOG_WARNING, "This may fail or give "
                   "incorrect results, depending on your hardware.\n");
        }
    }

    ctx->va_profile = profile;
    ctx->va_entrypoint = VAEntrypointVLD;

    vas = vaCreateConfig(hwctx->display, ctx->va_profile,
                         ctx->va_entrypoint, 0, 0, &ctx->va_config);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create decode pipeline "
               "configuration: %d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail;
    }

    hwconfig = av_hwdevice_hwconfig_alloc(ctx->device_ref);
    if (!hwconfig) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    hwconfig->config_id = ctx->va_config;

    constraints = av_hwdevice_get_hwframe_constraints(ctx->device_ref,
                                                      hwconfig);
    if (!constraints)
        goto fail;

    // Decide on the decoder target format.
    // If the user specified something with -hwaccel_output_format then
    // try to use that to minimise conversions later.
    ctx->decode_format = AV_PIX_FMT_NONE;
    if (ctx->output_format != AV_PIX_FMT_NONE &&
        ctx->output_format != AV_PIX_FMT_VAAPI) {
        for (i = 0; constraints->valid_sw_formats[i] != AV_PIX_FMT_NONE; i++) {
            if (constraints->valid_sw_formats[i] == ctx->decode_format) {
                ctx->decode_format = ctx->output_format;
                av_log(ctx, AV_LOG_DEBUG, "Using decode format %s (output "
                       "format).\n", av_get_pix_fmt_name(ctx->decode_format));
                break;
            }
        }
    }
    // Otherwise, we would like to try to choose something which matches the
    // decoder output, but there isn't enough information available here to
    // do so.  Assume for now that we are always dealing with YUV 4:2:0, so
    // pick a format which does that.
    if (ctx->decode_format == AV_PIX_FMT_NONE) {
        for (i = 0; constraints->valid_sw_formats[i] != AV_PIX_FMT_NONE; i++) {
            pix_fmt  = constraints->valid_sw_formats[i];
            pix_desc = av_pix_fmt_desc_get(pix_fmt);
            if (pix_desc->nb_components == 3 &&
                pix_desc->log2_chroma_w == 1 &&
                pix_desc->log2_chroma_h == 1) {
                ctx->decode_format = pix_fmt;
                av_log(ctx, AV_LOG_DEBUG, "Using decode format %s (format "
                       "matched).\n", av_get_pix_fmt_name(ctx->decode_format));
                break;
            }
        }
    }
    // Otherwise pick the first in the list and hope for the best.
    if (ctx->decode_format == AV_PIX_FMT_NONE) {
        ctx->decode_format = constraints->valid_sw_formats[0];
        av_log(ctx, AV_LOG_DEBUG, "Using decode format %s (first in list).\n",
               av_get_pix_fmt_name(ctx->decode_format));
        if (i > 1) {
            // There was a choice, and we picked randomly.  Warn the user
            // that they might want to choose intelligently instead.
            av_log(ctx, AV_LOG_WARNING, "Using randomly chosen decode "
                   "format %s.\n", av_get_pix_fmt_name(ctx->decode_format));
        }
    }

    // Ensure the picture size is supported by the hardware.
    ctx->decode_width  = avctx->coded_width;
    ctx->decode_height = avctx->coded_height;
    if (ctx->decode_width  < constraints->min_width  ||
        ctx->decode_height < constraints->min_height ||
        ctx->decode_width  > constraints->max_width  ||
        ctx->decode_height >constraints->max_height) {
        av_log(ctx, AV_LOG_ERROR, "VAAPI hardware does not support image "
               "size %dx%d (constraints: width %d-%d height %d-%d).\n",
               ctx->decode_width, ctx->decode_height,
               constraints->min_width,  constraints->max_width,
               constraints->min_height, constraints->max_height);
        err = AVERROR(EINVAL);
        goto fail;
    }

    av_hwframe_constraints_free(&constraints);
    av_freep(&hwconfig);

    // Decide how many reference frames we need.  This might be doable more
    // nicely based on the codec and input stream?
    ctx->decode_surfaces = DEFAULT_SURFACES;
    // For frame-threaded decoding, one additional surfaces is needed for
    // each thread.
    if (avctx->active_thread_type & FF_THREAD_FRAME)
        ctx->decode_surfaces += avctx->thread_count;

    return 0;

fail:
    av_hwframe_constraints_free(&constraints);
    av_freep(&hwconfig);
    vaDestroyConfig(hwctx->display, ctx->va_config);
    av_freep(&profile_list);
    return err;
}

static void vaapi_decode_uninit(AVCodecContext *avctx)
{
    InputStream *ist = avctx->opaque;
    VAAPIDecoderContext *ctx = ist->hwaccel_ctx;

    if (ctx) {
        AVVAAPIDeviceContext *hwctx = ctx->device->hwctx;

        if (ctx->va_context != VA_INVALID_ID) {
            vaDestroyContext(hwctx->display, ctx->va_context);
            ctx->va_context = VA_INVALID_ID;
        }
        if (ctx->va_config != VA_INVALID_ID) {
            vaDestroyConfig(hwctx->display, ctx->va_config);
            ctx->va_config = VA_INVALID_ID;
        }

        av_buffer_unref(&ctx->frames_ref);
        av_buffer_unref(&ctx->device_ref);
        av_free(ctx);
    }

    av_buffer_unref(&ist->hw_frames_ctx);

    ist->hwaccel_ctx           = 0;
    ist->hwaccel_uninit        = 0;
    ist->hwaccel_get_buffer    = 0;
    ist->hwaccel_retrieve_data = 0;
}

int vaapi_decode_init(AVCodecContext *avctx)
{
    InputStream *ist = avctx->opaque;
    AVVAAPIDeviceContext *hwctx;
    AVVAAPIFramesContext *avfc;
    VAAPIDecoderContext *ctx;
    VAStatus vas;
    int err;
    int loglevel = (ist->hwaccel_id != HWACCEL_VAAPI ? AV_LOG_VERBOSE
                                                     : AV_LOG_ERROR);

    if (ist->hwaccel_ctx)
        vaapi_decode_uninit(avctx);

    // We have -hwaccel without -vaapi_device, so just initialise here with
    // the device passed as -hwaccel_device (if -vaapi_device was passed, it
    // will always have been called before now).
    if (!hw_device_ctx) {
        err = vaapi_device_init(ist->hwaccel_device);
        if (err < 0)
            return err;
    }

    ctx = av_mallocz(sizeof(*ctx));
    if (!ctx)
        return AVERROR(ENOMEM);
    ctx->class = &vaapi_class;

    ctx->device_ref = av_buffer_ref(hw_device_ctx);
    ctx->device = (AVHWDeviceContext*)ctx->device_ref->data;

    ctx->va_config  = VA_INVALID_ID;
    ctx->va_context = VA_INVALID_ID;

    hwctx = ctx->device->hwctx;

    ctx->output_format = ist->hwaccel_output_format;

    err = vaapi_build_decoder_config(ctx, avctx,
                                     ist->hwaccel_id != HWACCEL_VAAPI);
    if (err < 0) {
        av_log(ctx, loglevel, "No supported configuration for this codec.");
        goto fail;
    }

    avctx->pix_fmt = ctx->output_format;

    ctx->frames_ref = av_hwframe_ctx_alloc(ctx->device_ref);
    if (!ctx->frames_ref) {
        av_log(ctx, loglevel, "Failed to create VAAPI frame context.\n");
        err = AVERROR(ENOMEM);
        goto fail;
    }

    ctx->frames = (AVHWFramesContext*)ctx->frames_ref->data;

    ctx->frames->format    = AV_PIX_FMT_VAAPI;
    ctx->frames->sw_format = ctx->decode_format;
    ctx->frames->width     = ctx->decode_width;
    ctx->frames->height    = ctx->decode_height;
    ctx->frames->initial_pool_size = ctx->decode_surfaces;

    err = av_hwframe_ctx_init(ctx->frames_ref);
    if (err < 0) {
        av_log(ctx, loglevel, "Failed to initialise VAAPI frame "
               "context: %d\n", err);
        goto fail;
    }

    avfc = ctx->frames->hwctx;

    vas = vaCreateContext(hwctx->display, ctx->va_config,
                          ctx->decode_width, ctx->decode_height,
                          VA_PROGRESSIVE,
                          avfc->surface_ids, avfc->nb_surfaces,
                          &ctx->va_context);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create decode pipeline "
               "context: %d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EINVAL);
        goto fail;
    }

    av_log(ctx, AV_LOG_DEBUG, "VAAPI decoder (re)init complete.\n");

    // We would like to set this on the AVCodecContext for use by whoever gets
    // the frames from the decoder, but unfortunately the AVCodecContext we
    // have here need not be the "real" one (H.264 makes many copies for
    // threading purposes).  To avoid the problem, we instead store it in the
    // InputStream and propagate it from there.
    ist->hw_frames_ctx = av_buffer_ref(ctx->frames_ref);
    if (!ist->hw_frames_ctx) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    ist->hwaccel_ctx           = ctx;
    ist->hwaccel_uninit        = vaapi_decode_uninit;
    ist->hwaccel_get_buffer    = vaapi_get_buffer;
    ist->hwaccel_retrieve_data = vaapi_retrieve_data;

    ctx->decoder_vaapi_context.display    = hwctx->display;
    ctx->decoder_vaapi_context.config_id  = ctx->va_config;
    ctx->decoder_vaapi_context.context_id = ctx->va_context;
    avctx->hwaccel_context = &ctx->decoder_vaapi_context;

    return 0;

fail:
    vaapi_decode_uninit(avctx);
    return err;
}

static AVClass *vaapi_log = &vaapi_class;

av_cold int vaapi_device_init(const char *device)
{
    int err;

    err = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI,
                                 device, NULL, 0);
    if (err < 0) {
        av_log(&vaapi_log, AV_LOG_ERROR, "Failed to create a VAAPI device\n");
        return err;
    }

    return 0;
}
