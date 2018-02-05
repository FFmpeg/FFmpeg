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
#include "libavutil/common.h"
#include "libavutil/pixdesc.h"

#include "avcodec.h"
#include "decode.h"
#include "internal.h"
#include "vaapi_decode.h"


int ff_vaapi_decode_make_param_buffer(AVCodecContext *avctx,
                                      VAAPIDecodePicture *pic,
                                      int type,
                                      const void *data,
                                      size_t size)
{
    VAAPIDecodeContext *ctx = avctx->internal->hwaccel_priv_data;
    VAStatus vas;
    VABufferID buffer;

    av_assert0(pic->nb_param_buffers + 1 <= MAX_PARAM_BUFFERS);

    vas = vaCreateBuffer(ctx->hwctx->display, ctx->va_context,
                         type, size, 1, (void*)data, &buffer);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create parameter "
               "buffer (type %d): %d (%s).\n",
               type, vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }

    pic->param_buffers[pic->nb_param_buffers++] = buffer;

    av_log(avctx, AV_LOG_DEBUG, "Param buffer (type %d, %zu bytes) "
           "is %#x.\n", type, size, buffer);
    return 0;
}


int ff_vaapi_decode_make_slice_buffer(AVCodecContext *avctx,
                                      VAAPIDecodePicture *pic,
                                      const void *params_data,
                                      size_t params_size,
                                      const void *slice_data,
                                      size_t slice_size)
{
    VAAPIDecodeContext *ctx = avctx->internal->hwaccel_priv_data;
    VAStatus vas;
    int index;

    av_assert0(pic->nb_slices <= pic->slices_allocated);
    if (pic->nb_slices == pic->slices_allocated) {
        if (pic->slices_allocated > 0)
            pic->slices_allocated *= 2;
        else
            pic->slices_allocated = 64;

        pic->slice_buffers =
            av_realloc_array(pic->slice_buffers,
                             pic->slices_allocated,
                             2 * sizeof(*pic->slice_buffers));
        if (!pic->slice_buffers)
            return AVERROR(ENOMEM);
    }
    av_assert0(pic->nb_slices + 1 <= pic->slices_allocated);

    index = 2 * pic->nb_slices;

    vas = vaCreateBuffer(ctx->hwctx->display, ctx->va_context,
                         VASliceParameterBufferType,
                         params_size, 1, (void*)params_data,
                         &pic->slice_buffers[index]);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create slice "
               "parameter buffer: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }

    av_log(avctx, AV_LOG_DEBUG, "Slice %d param buffer (%zu bytes) "
           "is %#x.\n", pic->nb_slices, params_size,
           pic->slice_buffers[index]);

    vas = vaCreateBuffer(ctx->hwctx->display, ctx->va_context,
                         VASliceDataBufferType,
                         slice_size, 1, (void*)slice_data,
                         &pic->slice_buffers[index + 1]);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create slice "
               "data buffer (size %zu): %d (%s).\n",
               slice_size, vas, vaErrorStr(vas));
        vaDestroyBuffer(ctx->hwctx->display,
                        pic->slice_buffers[index]);
        return AVERROR(EIO);
    }

    av_log(avctx, AV_LOG_DEBUG, "Slice %d data buffer (%zu bytes) "
           "is %#x.\n", pic->nb_slices, slice_size,
           pic->slice_buffers[index + 1]);

    ++pic->nb_slices;
    return 0;
}

static void ff_vaapi_decode_destroy_buffers(AVCodecContext *avctx,
                                            VAAPIDecodePicture *pic)
{
    VAAPIDecodeContext *ctx = avctx->internal->hwaccel_priv_data;
    VAStatus vas;
    int i;

    for (i = 0; i < pic->nb_param_buffers; i++) {
        vas = vaDestroyBuffer(ctx->hwctx->display,
                              pic->param_buffers[i]);
        if (vas != VA_STATUS_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to destroy "
                   "parameter buffer %#x: %d (%s).\n",
                   pic->param_buffers[i], vas, vaErrorStr(vas));
        }
    }

    for (i = 0; i < 2 * pic->nb_slices; i++) {
        vas = vaDestroyBuffer(ctx->hwctx->display,
                              pic->slice_buffers[i]);
        if (vas != VA_STATUS_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to destroy slice "
                   "slice buffer %#x: %d (%s).\n",
                   pic->slice_buffers[i], vas, vaErrorStr(vas));
        }
    }
}

int ff_vaapi_decode_issue(AVCodecContext *avctx,
                          VAAPIDecodePicture *pic)
{
    VAAPIDecodeContext *ctx = avctx->internal->hwaccel_priv_data;
    VAStatus vas;
    int err;

    av_log(avctx, AV_LOG_DEBUG, "Decode to surface %#x.\n",
           pic->output_surface);

    vas = vaBeginPicture(ctx->hwctx->display, ctx->va_context,
                         pic->output_surface);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to begin picture decode "
               "issue: %d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail_with_picture;
    }

    vas = vaRenderPicture(ctx->hwctx->display, ctx->va_context,
                          pic->param_buffers, pic->nb_param_buffers);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to upload decode "
               "parameters: %d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail_with_picture;
    }

    vas = vaRenderPicture(ctx->hwctx->display, ctx->va_context,
                          pic->slice_buffers, 2 * pic->nb_slices);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to upload slices: "
               "%d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail_with_picture;
    }

    vas = vaEndPicture(ctx->hwctx->display, ctx->va_context);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to end picture decode "
               "issue: %d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        if (CONFIG_VAAPI_1 || ctx->hwctx->driver_quirks &
            AV_VAAPI_DRIVER_QUIRK_RENDER_PARAM_BUFFERS)
            goto fail;
        else
            goto fail_at_end;
    }

    if (CONFIG_VAAPI_1 || ctx->hwctx->driver_quirks &
        AV_VAAPI_DRIVER_QUIRK_RENDER_PARAM_BUFFERS)
        ff_vaapi_decode_destroy_buffers(avctx, pic);

    pic->nb_param_buffers = 0;
    pic->nb_slices        = 0;
    pic->slices_allocated = 0;
    av_freep(&pic->slice_buffers);

    return 0;

fail_with_picture:
    vas = vaEndPicture(ctx->hwctx->display, ctx->va_context);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to end picture decode "
               "after error: %d (%s).\n", vas, vaErrorStr(vas));
    }
fail:
    ff_vaapi_decode_destroy_buffers(avctx, pic);
fail_at_end:
    return err;
}

int ff_vaapi_decode_cancel(AVCodecContext *avctx,
                           VAAPIDecodePicture *pic)
{
    ff_vaapi_decode_destroy_buffers(avctx, pic);

    pic->nb_param_buffers = 0;
    pic->nb_slices        = 0;
    pic->slices_allocated = 0;
    av_freep(&pic->slice_buffers);

    return 0;
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
    MAP(H264,        H264_MAIN,       H264Main    ),
    MAP(H264,        H264_HIGH,       H264High    ),
#if VA_CHECK_VERSION(0, 37, 0)
    MAP(HEVC,        HEVC_MAIN,       HEVCMain    ),
    MAP(HEVC,        HEVC_MAIN_10,    HEVCMain10  ),
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
#if VA_CHECK_VERSION(0, 38, 0)
    MAP(VP9,         VP9_0,           VP9Profile0 ),
#endif
#if VA_CHECK_VERSION(0, 39, 0)
    MAP(VP9,         VP9_2,           VP9Profile2 ),
#endif
#undef MAP
};

/*
 * Set *va_config and the frames_ref fields from the current codec parameters
 * in avctx.
 */
static int vaapi_decode_make_config(AVCodecContext *avctx,
                                    AVBufferRef *device_ref,
                                    VAConfigID *va_config,
                                    AVBufferRef *frames_ref)
{
    AVVAAPIHWConfig       *hwconfig    = NULL;
    AVHWFramesConstraints *constraints = NULL;
    VAStatus vas;
    int err, i, j;
    const AVCodecDescriptor *codec_desc;
    VAProfile *profile_list = NULL, matched_va_profile;
    int profile_count, exact_match, matched_ff_profile;
    const AVPixFmtDescriptor *sw_desc, *desc;

    AVHWDeviceContext    *device = (AVHWDeviceContext*)device_ref->data;
    AVVAAPIDeviceContext *hwctx = device->hwctx;

    codec_desc = avcodec_descriptor_get(avctx->codec_id);
    if (!codec_desc) {
        err = AVERROR(EINVAL);
        goto fail;
    }

    profile_count = vaMaxNumProfiles(hwctx->display);
    profile_list  = av_malloc_array(profile_count,
                                    sizeof(VAProfile));
    if (!profile_list) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    vas = vaQueryConfigProfiles(hwctx->display,
                                profile_list, &profile_count);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query profiles: "
               "%d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(ENOSYS);
        goto fail;
    }

    matched_va_profile = VAProfileNone;
    exact_match = 0;

    for (i = 0; i < FF_ARRAY_ELEMS(vaapi_profile_map); i++) {
        int profile_match = 0;
        if (avctx->codec_id != vaapi_profile_map[i].codec_id)
            continue;
        if (avctx->profile == vaapi_profile_map[i].codec_profile ||
            vaapi_profile_map[i].codec_profile == FF_PROFILE_UNKNOWN)
            profile_match = 1;
        for (j = 0; j < profile_count; j++) {
            if (vaapi_profile_map[i].va_profile == profile_list[j]) {
                exact_match = profile_match;
                break;
            }
        }
        if (j < profile_count) {
            matched_va_profile = vaapi_profile_map[i].va_profile;
            matched_ff_profile = vaapi_profile_map[i].codec_profile;
            if (exact_match)
                break;
        }
    }
    av_freep(&profile_list);

    if (matched_va_profile == VAProfileNone) {
        av_log(avctx, AV_LOG_ERROR, "No support for codec %s "
               "profile %d.\n", codec_desc->name, avctx->profile);
        err = AVERROR(ENOSYS);
        goto fail;
    }
    if (!exact_match) {
        if (avctx->hwaccel_flags &
            AV_HWACCEL_FLAG_ALLOW_PROFILE_MISMATCH) {
            av_log(avctx, AV_LOG_VERBOSE, "Codec %s profile %d not "
                   "supported for hardware decode.\n",
                   codec_desc->name, avctx->profile);
            av_log(avctx, AV_LOG_WARNING, "Using possibly-"
                   "incompatible profile %d instead.\n",
                   matched_ff_profile);
        } else {
            av_log(avctx, AV_LOG_VERBOSE, "Codec %s profile %d not "
                   "supported for hardware decode.\n",
                   codec_desc->name, avctx->profile);
            err = AVERROR(EINVAL);
            goto fail;
        }
    }

    vas = vaCreateConfig(hwctx->display, matched_va_profile,
                         VAEntrypointVLD, NULL, 0,
                         va_config);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create decode "
               "configuration: %d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail;
    }

    hwconfig = av_hwdevice_hwconfig_alloc(device_ref);
    if (!hwconfig) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    hwconfig->config_id = *va_config;

    constraints =
        av_hwdevice_get_hwframe_constraints(device_ref, hwconfig);
    if (!constraints) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    if (avctx->coded_width  < constraints->min_width  ||
        avctx->coded_height < constraints->min_height ||
        avctx->coded_width  > constraints->max_width  ||
        avctx->coded_height > constraints->max_height) {
        av_log(avctx, AV_LOG_ERROR, "Hardware does not support image "
               "size %dx%d (constraints: width %d-%d height %d-%d).\n",
               avctx->coded_width, avctx->coded_height,
               constraints->min_width,  constraints->max_width,
               constraints->min_height, constraints->max_height);
        err = AVERROR(EINVAL);
        goto fail;
    }
    if (!constraints->valid_sw_formats ||
        constraints->valid_sw_formats[0] == AV_PIX_FMT_NONE) {
        av_log(avctx, AV_LOG_ERROR, "Hardware does not offer any "
               "usable surface formats.\n");
        err = AVERROR(EINVAL);
        goto fail;
    }

    if (frames_ref) {
        AVHWFramesContext *frames = (AVHWFramesContext *)frames_ref->data;

        frames->format = AV_PIX_FMT_VAAPI;
        frames->width = avctx->coded_width;
        frames->height = avctx->coded_height;

        // Find the first format in the list which matches the expected
        // bit depth and subsampling.  If none are found (this can happen
        // when 10-bit streams are decoded to 8-bit surfaces, for example)
        // then just take the first format on the list.
        frames->sw_format = constraints->valid_sw_formats[0];
        sw_desc = av_pix_fmt_desc_get(avctx->sw_pix_fmt);
        for (i = 0; constraints->valid_sw_formats[i] != AV_PIX_FMT_NONE; i++) {
            desc = av_pix_fmt_desc_get(constraints->valid_sw_formats[i]);
            if (desc->nb_components != sw_desc->nb_components ||
                desc->log2_chroma_w != sw_desc->log2_chroma_w ||
                desc->log2_chroma_h != sw_desc->log2_chroma_h)
                continue;
            for (j = 0; j < desc->nb_components; j++) {
                if (desc->comp[j].depth != sw_desc->comp[j].depth)
                    break;
            }
            if (j < desc->nb_components)
                continue;
            frames->sw_format = constraints->valid_sw_formats[i];
            break;
        }

        frames->initial_pool_size = 1;
        // Add per-codec number of surfaces used for storing reference frames.
        switch (avctx->codec_id) {
        case AV_CODEC_ID_H264:
        case AV_CODEC_ID_HEVC:
            frames->initial_pool_size += 16;
            break;
        case AV_CODEC_ID_VP9:
            frames->initial_pool_size += 8;
            break;
        case AV_CODEC_ID_VP8:
            frames->initial_pool_size += 3;
            break;
        default:
            frames->initial_pool_size += 2;
        }
    }

    av_hwframe_constraints_free(&constraints);
    av_freep(&hwconfig);

    return 0;

fail:
    av_hwframe_constraints_free(&constraints);
    av_freep(&hwconfig);
    if (*va_config != VA_INVALID_ID) {
        vaDestroyConfig(hwctx->display, *va_config);
        *va_config = VA_INVALID_ID;
    }
    av_freep(&profile_list);
    return err;
}

int ff_vaapi_common_frame_params(AVCodecContext *avctx,
                                 AVBufferRef *hw_frames_ctx)
{
    AVHWFramesContext *hw_frames = (AVHWFramesContext *)hw_frames_ctx->data;
    AVHWDeviceContext *device_ctx = hw_frames->device_ctx;
    AVVAAPIDeviceContext *hwctx;
    VAConfigID va_config = VA_INVALID_ID;
    int err;

    if (device_ctx->type != AV_HWDEVICE_TYPE_VAAPI)
        return AVERROR(EINVAL);
    hwctx = device_ctx->hwctx;

    err = vaapi_decode_make_config(avctx, hw_frames->device_ref, &va_config,
                                   hw_frames_ctx);
    if (err)
        return err;

    if (va_config != VA_INVALID_ID)
        vaDestroyConfig(hwctx->display, va_config);

    return 0;
}

int ff_vaapi_decode_init(AVCodecContext *avctx)
{
    VAAPIDecodeContext *ctx = avctx->internal->hwaccel_priv_data;
    VAStatus vas;
    int err;

    ctx->va_config  = VA_INVALID_ID;
    ctx->va_context = VA_INVALID_ID;

#if FF_API_STRUCT_VAAPI_CONTEXT
    if (avctx->hwaccel_context) {
        av_log(avctx, AV_LOG_WARNING, "Using deprecated struct "
               "vaapi_context in decode.\n");

        ctx->have_old_context = 1;
        ctx->old_context = avctx->hwaccel_context;

        // Really we only want the VAAPI device context, but this
        // allocates a whole generic device context because we don't
        // have any other way to determine how big it should be.
        ctx->device_ref =
            av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VAAPI);
        if (!ctx->device_ref) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
        ctx->device = (AVHWDeviceContext*)ctx->device_ref->data;
        ctx->hwctx  = ctx->device->hwctx;

        ctx->hwctx->display = ctx->old_context->display;

        // The old VAAPI decode setup assumed this quirk was always
        // present, so set it here to avoid the behaviour changing.
        ctx->hwctx->driver_quirks =
            AV_VAAPI_DRIVER_QUIRK_RENDER_PARAM_BUFFERS;

    }
#endif

#if FF_API_STRUCT_VAAPI_CONTEXT
    if (ctx->have_old_context) {
        ctx->va_config  = ctx->old_context->config_id;
        ctx->va_context = ctx->old_context->context_id;

        av_log(avctx, AV_LOG_DEBUG, "Using user-supplied decoder "
               "context: %#x/%#x.\n", ctx->va_config, ctx->va_context);
    } else {
#endif

    err = ff_decode_get_hw_frames_ctx(avctx, AV_HWDEVICE_TYPE_VAAPI);
    if (err < 0)
        goto fail;

    ctx->frames = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
    ctx->hwfc   = ctx->frames->hwctx;
    ctx->device = ctx->frames->device_ctx;
    ctx->hwctx  = ctx->device->hwctx;

    err = vaapi_decode_make_config(avctx, ctx->frames->device_ref,
                                   &ctx->va_config, avctx->hw_frames_ctx);
    if (err)
        goto fail;

    vas = vaCreateContext(ctx->hwctx->display, ctx->va_config,
                          avctx->coded_width, avctx->coded_height,
                          VA_PROGRESSIVE,
                          ctx->hwfc->surface_ids,
                          ctx->hwfc->nb_surfaces,
                          &ctx->va_context);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create decode "
               "context: %d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail;
    }

    av_log(avctx, AV_LOG_DEBUG, "Decode context initialised: "
           "%#x/%#x.\n", ctx->va_config, ctx->va_context);
#if FF_API_STRUCT_VAAPI_CONTEXT
    }
#endif

    return 0;

fail:
    ff_vaapi_decode_uninit(avctx);
    return err;
}

int ff_vaapi_decode_uninit(AVCodecContext *avctx)
{
    VAAPIDecodeContext *ctx = avctx->internal->hwaccel_priv_data;
    VAStatus vas;

#if FF_API_STRUCT_VAAPI_CONTEXT
    if (ctx->have_old_context) {
        av_buffer_unref(&ctx->device_ref);
    } else {
#endif

    if (ctx->va_context != VA_INVALID_ID) {
        vas = vaDestroyContext(ctx->hwctx->display, ctx->va_context);
        if (vas != VA_STATUS_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to destroy decode "
                   "context %#x: %d (%s).\n",
                   ctx->va_context, vas, vaErrorStr(vas));
        }
    }
    if (ctx->va_config != VA_INVALID_ID) {
        vas = vaDestroyConfig(ctx->hwctx->display, ctx->va_config);
        if (vas != VA_STATUS_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to destroy decode "
                   "configuration %#x: %d (%s).\n",
                   ctx->va_config, vas, vaErrorStr(vas));
        }
    }

#if FF_API_STRUCT_VAAPI_CONTEXT
    }
#endif

    return 0;
}
