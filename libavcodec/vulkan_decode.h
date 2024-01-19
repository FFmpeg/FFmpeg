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

#ifndef AVCODEC_VULKAN_DECODE_H
#define AVCODEC_VULKAN_DECODE_H

#include "codec_id.h"
#include "decode.h"
#include "hwaccel_internal.h"
#include "internal.h"

#include "vulkan_video.h"

typedef struct FFVulkanDecodeDescriptor {
    enum AVCodecID                   codec_id;
    FFVulkanExtensions               decode_extension;
    VkVideoCodecOperationFlagBitsKHR decode_op;

    VkExtensionProperties ext_props;
} FFVulkanDecodeDescriptor;

typedef struct FFVulkanDecodeProfileData {
    VkVideoDecodeH264ProfileInfoKHR h264_profile;
    VkVideoDecodeH265ProfileInfoKHR h265_profile;
    VkVideoDecodeAV1ProfileInfoKHR av1_profile;
    VkVideoDecodeUsageInfoKHR usage;
    VkVideoProfileInfoKHR profile;
    VkVideoProfileListInfoKHR profile_list;
} FFVulkanDecodeProfileData;

typedef struct FFVulkanDecodeShared {
    FFVulkanContext s;
    FFVkVideoCommon common;
    FFVkQueueFamilyCtx qf;

    VkVideoCapabilitiesKHR caps;
    VkVideoDecodeCapabilitiesKHR dec_caps;

    AVBufferRef *dpb_hwfc_ref;  /* Only used for dedicated_dpb */

    AVFrame *layered_frame;     /* Only used for layered_dpb   */
    VkImageView layered_view;
    VkImageAspectFlags layered_aspect;

    VkVideoSessionParametersKHR empty_session_params;

    VkSamplerYcbcrConversion yuv_sampler;
} FFVulkanDecodeShared;

typedef struct FFVulkanDecodeContext {
    FFVulkanDecodeShared *shared_ctx;
    AVBufferRef *session_params;
    FFVkExecPool exec_pool;

    int dedicated_dpb; /* Oddity  #1 - separate DPB images */
    int layered_dpb;   /* Madness #1 - layered  DPB images */
    int external_fg;   /* Oddity  #2 - hardware can't apply film grain */
    uint32_t frame_id_alloc_mask; /* For AV1 only */

    /* Thread-local state below */
    struct HEVCHeaderSet *hevc_headers;
    size_t hevc_headers_size;

    uint32_t                       *slice_off;
    unsigned int                    slice_off_max;
} FFVulkanDecodeContext;

typedef struct FFVulkanDecodePicture {
    AVFrame                        *dpb_frame;      /* Only used for out-of-place decoding. */

    VkImageView                     img_view_ref;   /* Image representation view (reference) */
    VkImageView                     img_view_out;   /* Image representation view (output-only) */
    VkImageView                     img_view_dest;  /* Set to img_view_out if no layered refs are used */
    VkImageAspectFlags              img_aspect;     /* Image plane mask bits */
    VkImageAspectFlags              img_aspect_ref; /* Only used for out-of-place decoding */

    VkSemaphore                     sem;
    uint64_t                        sem_value;

    /* Current picture */
    VkVideoPictureResourceInfoKHR   ref;
    VkVideoReferenceSlotInfoKHR     ref_slot;

    /* Picture refs. H264 has the maximum number of refs (36) of any supported codec. */
    VkVideoPictureResourceInfoKHR   refs     [36];
    VkVideoReferenceSlotInfoKHR     ref_slots[36];

    /* Main decoding struct */
    VkVideoDecodeInfoKHR            decode_info;

    /* Slice data */
    AVBufferRef                    *slices_buf;
    size_t                          slices_size;

    /* Vulkan functions needed for destruction, as no other context is guaranteed to exist */
    PFN_vkWaitSemaphores            wait_semaphores;
    PFN_vkDestroyImageView          destroy_image_view;
} FFVulkanDecodePicture;

/**
 * Initialize decoder.
 */
int ff_vk_decode_init(AVCodecContext *avctx);

/**
 * Synchronize the contexts between 2 threads.
 */
int ff_vk_update_thread_context(AVCodecContext *dst, const AVCodecContext *src);

/**
 * Initialize hw_frames_ctx with the parameters needed to decode the stream
 * using the parameters from avctx.
 *
 * NOTE: if avctx->internal->hwaccel_priv_data exists, will partially initialize
 * the context.
 */
int ff_vk_frame_params(AVCodecContext *avctx, AVBufferRef *hw_frames_ctx);

/**
 * Removes current session parameters to recreate them
 */
int ff_vk_params_invalidate(AVCodecContext *avctx, int t, const uint8_t *b, uint32_t s);

/**
 * Prepare a frame, creates the image view, and sets up the dpb fields.
 */
int ff_vk_decode_prepare_frame(FFVulkanDecodeContext *dec, AVFrame *pic,
                               FFVulkanDecodePicture *vkpic, int is_current,
                               int alloc_dpb);

/**
 * Add slice data to frame.
 */
int ff_vk_decode_add_slice(AVCodecContext *avctx, FFVulkanDecodePicture *vp,
                           const uint8_t *data, size_t size, int add_startcode,
                           uint32_t *nb_slices, const uint32_t **offsets);

/**
 * Decode a frame.
 */
int ff_vk_decode_frame(AVCodecContext *avctx,
                       AVFrame *pic,    FFVulkanDecodePicture *vp,
                       AVFrame *rpic[], FFVulkanDecodePicture *rvkp[]);

/**
 * Free a frame and its state.
 */
void ff_vk_decode_free_frame(AVHWDeviceContext *dev_ctx, FFVulkanDecodePicture *vp);

/**
 * Get an FFVkBuffer suitable for decoding from.
 */
int ff_vk_get_decode_buffer(FFVulkanDecodeContext *ctx, AVBufferRef **buf,
                            void *create_pNext, size_t size);

/**
 * Create VkVideoSessionParametersKHR wrapped in an AVBufferRef.
 */
int ff_vk_decode_create_params(AVBufferRef **par_ref, void *logctx, FFVulkanDecodeShared *ctx,
                               const VkVideoSessionParametersCreateInfoKHR *session_params_create);

/**
 * Flush decoder.
 */
void ff_vk_decode_flush(AVCodecContext *avctx);

/**
 * Free decoder.
 */
int ff_vk_decode_uninit(AVCodecContext *avctx);

#endif /* AVCODEC_VULKAN_DECODE_H */
