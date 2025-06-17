/*
 * Copyright (c) Lynne
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

#include "avassert.h"
#include "mem.h"

#include "vulkan.h"
#include "libavutil/vulkan_loader.h"

const VkComponentMapping ff_comp_identity_map = {
    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
};

/* Converts return values to strings */
const char *ff_vk_ret2str(VkResult res)
{
#define CASE(VAL) case VAL: return #VAL
    switch (res) {
    CASE(VK_SUCCESS);
    CASE(VK_NOT_READY);
    CASE(VK_TIMEOUT);
    CASE(VK_EVENT_SET);
    CASE(VK_EVENT_RESET);
    CASE(VK_INCOMPLETE);
    CASE(VK_ERROR_OUT_OF_HOST_MEMORY);
    CASE(VK_ERROR_OUT_OF_DEVICE_MEMORY);
    CASE(VK_ERROR_INITIALIZATION_FAILED);
    CASE(VK_ERROR_DEVICE_LOST);
    CASE(VK_ERROR_MEMORY_MAP_FAILED);
    CASE(VK_ERROR_LAYER_NOT_PRESENT);
    CASE(VK_ERROR_EXTENSION_NOT_PRESENT);
    CASE(VK_ERROR_FEATURE_NOT_PRESENT);
    CASE(VK_ERROR_INCOMPATIBLE_DRIVER);
    CASE(VK_ERROR_TOO_MANY_OBJECTS);
    CASE(VK_ERROR_FORMAT_NOT_SUPPORTED);
    CASE(VK_ERROR_FRAGMENTED_POOL);
    CASE(VK_ERROR_UNKNOWN);
    CASE(VK_ERROR_OUT_OF_POOL_MEMORY);
    CASE(VK_ERROR_INVALID_EXTERNAL_HANDLE);
    CASE(VK_ERROR_FRAGMENTATION);
    CASE(VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS);
    CASE(VK_PIPELINE_COMPILE_REQUIRED);
    CASE(VK_ERROR_SURFACE_LOST_KHR);
    CASE(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
    CASE(VK_SUBOPTIMAL_KHR);
    CASE(VK_ERROR_OUT_OF_DATE_KHR);
    CASE(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR);
    CASE(VK_ERROR_VALIDATION_FAILED_EXT);
    CASE(VK_ERROR_INVALID_SHADER_NV);
    CASE(VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR);
    CASE(VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR);
    CASE(VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR);
    CASE(VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR);
    CASE(VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR);
    CASE(VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);
    CASE(VK_ERROR_NOT_PERMITTED_KHR);
    CASE(VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT);
    CASE(VK_THREAD_IDLE_KHR);
    CASE(VK_THREAD_DONE_KHR);
    CASE(VK_OPERATION_DEFERRED_KHR);
    CASE(VK_OPERATION_NOT_DEFERRED_KHR);
    default: return "Unknown error";
    }
#undef CASE
}

/* Malitia pura, Khronos */
#define FN_MAP_TO(dst_t, dst_name, src_t, src_name)                                 \
    dst_t ff_vk_map_ ##src_name## _to_ ##dst_name(src_t src) \
    {                                                                   \
        dst_t dst = 0x0;                                                \
        MAP_TO(VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT,                   \
               VK_IMAGE_USAGE_SAMPLED_BIT);                             \
        MAP_TO(VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT,                    \
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT);                        \
        MAP_TO(VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT,                    \
               VK_IMAGE_USAGE_TRANSFER_DST_BIT);                        \
        MAP_TO(VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT,                   \
               VK_IMAGE_USAGE_STORAGE_BIT);                             \
        MAP_TO(VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT,                \
               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);                    \
        MAP_TO(VK_FORMAT_FEATURE_2_VIDEO_DECODE_OUTPUT_BIT_KHR,         \
               VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR);                \
        MAP_TO(VK_FORMAT_FEATURE_2_VIDEO_DECODE_DPB_BIT_KHR,            \
               VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR);                \
        MAP_TO(VK_FORMAT_FEATURE_2_VIDEO_ENCODE_DPB_BIT_KHR,            \
               VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR);                \
        MAP_TO(VK_FORMAT_FEATURE_2_VIDEO_ENCODE_INPUT_BIT_KHR,          \
               VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR);                \
        MAP_TO(VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT_EXT,         \
               VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT);                   \
        return dst;                                                     \
    }

#define MAP_TO(flag1, flag2) if (src & flag2) dst |= flag1;
FN_MAP_TO(VkFormatFeatureFlagBits2, feats, VkImageUsageFlags, usage)
#undef MAP_TO
#define MAP_TO(flag1, flag2) if (src & flag1) dst |= flag2;
FN_MAP_TO(VkImageUsageFlags, usage, VkFormatFeatureFlagBits2, feats)
#undef MAP_TO
#undef FN_MAP_TO

static void load_enabled_qfs(FFVulkanContext *s)
{
    s->nb_qfs = 0;
    for (int i = 0; i < s->hwctx->nb_qf; i++) {
        /* Skip duplicates */
        int skip = 0;
        for (int j = 0; j < s->nb_qfs; j++) {
            if (s->qfs[j] == s->hwctx->qf[i].idx) {
                skip = 1;
                break;
            }
        }
        if (skip)
            continue;

        s->qfs[s->nb_qfs++] = s->hwctx->qf[i].idx;
    }
}

int ff_vk_load_props(FFVulkanContext *s)
{
    FFVulkanFunctions *vk = &s->vkfn;

    s->props = (VkPhysicalDeviceProperties2) {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    };

    FF_VK_STRUCT_EXT(s, &s->props, &s->props_11, FF_VK_EXT_NO_FLAG,
                     VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES);
    FF_VK_STRUCT_EXT(s, &s->props, &s->driver_props, FF_VK_EXT_NO_FLAG,
                     VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES);
    FF_VK_STRUCT_EXT(s, &s->props, &s->subgroup_props, FF_VK_EXT_NO_FLAG,
                     VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES);

    FF_VK_STRUCT_EXT(s, &s->props, &s->push_desc_props, FF_VK_EXT_PUSH_DESCRIPTOR,
                     VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR);
    FF_VK_STRUCT_EXT(s, &s->props, &s->hprops, FF_VK_EXT_EXTERNAL_HOST_MEMORY,
                     VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT);
    FF_VK_STRUCT_EXT(s, &s->props, &s->coop_matrix_props, FF_VK_EXT_COOP_MATRIX,
                     VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_PROPERTIES_KHR);
    FF_VK_STRUCT_EXT(s, &s->props, &s->desc_buf_props, FF_VK_EXT_DESCRIPTOR_BUFFER,
                     VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT);
    FF_VK_STRUCT_EXT(s, &s->props, &s->optical_flow_props, FF_VK_EXT_OPTICAL_FLOW,
                     VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_PROPERTIES_NV);
    FF_VK_STRUCT_EXT(s, &s->props, &s->host_image_props, FF_VK_EXT_HOST_IMAGE_COPY,
                     VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_PROPERTIES_EXT);

    s->feats = (VkPhysicalDeviceFeatures2) {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
    };

    FF_VK_STRUCT_EXT(s, &s->feats, &s->feats_12, FF_VK_EXT_NO_FLAG,
                     VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    FF_VK_STRUCT_EXT(s, &s->feats, &s->atomic_float_feats, FF_VK_EXT_ATOMIC_FLOAT,
                     VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT);

    /* Try allocating 1024 layouts */
    s->host_image_copy_layouts = av_malloc(sizeof(*s->host_image_copy_layouts)*1024);
    s->host_image_props.pCopySrcLayouts = s->host_image_copy_layouts;
    s->host_image_props.copySrcLayoutCount = 512;
    s->host_image_props.pCopyDstLayouts = s->host_image_copy_layouts + 512;
    s->host_image_props.copyDstLayoutCount = 512;

    vk->GetPhysicalDeviceProperties2(s->hwctx->phys_dev, &s->props);

    /* Check if we had enough memory for all layouts */
    if (s->host_image_props.copySrcLayoutCount == 512 ||
        s->host_image_props.copyDstLayoutCount == 512) {
        VkImageLayout *new_array;
        size_t new_size;
        s->host_image_props.pCopySrcLayouts =
        s->host_image_props.pCopyDstLayouts = NULL;
        s->host_image_props.copySrcLayoutCount =
        s->host_image_props.copyDstLayoutCount = 0;
        vk->GetPhysicalDeviceProperties2(s->hwctx->phys_dev, &s->props);

        new_size = s->host_image_props.copySrcLayoutCount +
                   s->host_image_props.copyDstLayoutCount;
        new_size *= sizeof(*s->host_image_copy_layouts);
        new_array = av_realloc(s->host_image_copy_layouts, new_size);
        if (!new_array)
            return AVERROR(ENOMEM);

        s->host_image_copy_layouts = new_array;
        s->host_image_props.pCopySrcLayouts = new_array;
        s->host_image_props.pCopyDstLayouts = new_array + s->host_image_props.copySrcLayoutCount;
        vk->GetPhysicalDeviceProperties2(s->hwctx->phys_dev, &s->props);
    }

    vk->GetPhysicalDeviceMemoryProperties(s->hwctx->phys_dev, &s->mprops);
    vk->GetPhysicalDeviceFeatures2(s->hwctx->phys_dev, &s->feats);

    load_enabled_qfs(s);

    if (s->qf_props)
        return 0;

    vk->GetPhysicalDeviceQueueFamilyProperties2(s->hwctx->phys_dev, &s->tot_nb_qfs, NULL);

    s->qf_props = av_calloc(s->tot_nb_qfs, sizeof(*s->qf_props));
    if (!s->qf_props)
        return AVERROR(ENOMEM);

    s->query_props = av_calloc(s->tot_nb_qfs, sizeof(*s->query_props));
    if (!s->qf_props) {
        av_freep(&s->qf_props);
        return AVERROR(ENOMEM);
    }

    s->video_props = av_calloc(s->tot_nb_qfs, sizeof(*s->video_props));
    if (!s->video_props) {
        av_freep(&s->qf_props);
        av_freep(&s->query_props);
        return AVERROR(ENOMEM);
    }

    for (uint32_t i = 0; i < s->tot_nb_qfs; i++) {
        s->qf_props[i] = (VkQueueFamilyProperties2) {
            .sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2,
        };

        FF_VK_STRUCT_EXT(s, &s->qf_props[i], &s->query_props[i], FF_VK_EXT_NO_FLAG,
                         VK_STRUCTURE_TYPE_QUEUE_FAMILY_QUERY_RESULT_STATUS_PROPERTIES_KHR);
        FF_VK_STRUCT_EXT(s, &s->qf_props[i], &s->video_props[i], FF_VK_EXT_VIDEO_QUEUE,
                         VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR);
    }

    vk->GetPhysicalDeviceQueueFamilyProperties2(s->hwctx->phys_dev, &s->tot_nb_qfs, s->qf_props);

    if (s->extensions & FF_VK_EXT_COOP_MATRIX) {
        vk->GetPhysicalDeviceCooperativeMatrixPropertiesKHR(s->hwctx->phys_dev,
                                                            &s->coop_mat_props_nb, NULL);

        if (s->coop_mat_props_nb) {
            s->coop_mat_props = av_malloc_array(s->coop_mat_props_nb,
                                                sizeof(VkCooperativeMatrixPropertiesKHR));
            for (int i = 0; i < s->coop_mat_props_nb; i++) {
                s->coop_mat_props[i] = (VkCooperativeMatrixPropertiesKHR) {
                    .sType = VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR,
                };
            }

            vk->GetPhysicalDeviceCooperativeMatrixPropertiesKHR(s->hwctx->phys_dev,
                                                                &s->coop_mat_props_nb,
                                                                s->coop_mat_props);
        }
    }

    return 0;
}

AVVulkanDeviceQueueFamily *ff_vk_qf_find(FFVulkanContext *s,
                                         VkQueueFlagBits dev_family,
                                         VkVideoCodecOperationFlagBitsKHR vid_ops)
{
    for (int i = 0; i < s->hwctx->nb_qf; i++) {
        if ((s->hwctx->qf[i].flags & dev_family) &&
            (s->hwctx->qf[i].video_caps & vid_ops) == vid_ops) {
            return &s->hwctx->qf[i];
        }
    }
    return NULL;
}

void ff_vk_exec_pool_free(FFVulkanContext *s, FFVkExecPool *pool)
{
    FFVulkanFunctions *vk = &s->vkfn;

    for (int i = 0; i < pool->pool_size; i++) {
        FFVkExecContext *e = &pool->contexts[i];

        if (e->fence) {
            if (e->had_submission)
                vk->WaitForFences(s->hwctx->act_dev, 1, &e->fence, VK_TRUE, UINT64_MAX);
            vk->DestroyFence(s->hwctx->act_dev, e->fence, s->hwctx->alloc);
        }

        ff_vk_exec_discard_deps(s, e);

        av_free(e->frame_deps);
        av_free(e->sw_frame_deps);
        av_free(e->buf_deps);
        av_free(e->queue_family_dst);
        av_free(e->layout_dst);
        av_free(e->access_dst);
        av_free(e->frame_update);
        av_free(e->frame_locked);
        av_free(e->sem_sig);
        av_free(e->sem_sig_val_dst);
        av_free(e->sem_wait);
    }

    /* Free shader-specific data */
    for (int i = 0; i < pool->nb_reg_shd; i++) {
        FFVulkanShaderData *sd = &pool->reg_shd[i];

        if (s->extensions & FF_VK_EXT_DESCRIPTOR_BUFFER) {
            for (int j = 0; j < sd->nb_descriptor_sets; j++) {
                FFVulkanDescriptorSetData *set_data = &sd->desc_set_buf[j];
                if (set_data->buf.mem)
                    ff_vk_unmap_buffer(s, &set_data->buf, 0);
                ff_vk_free_buf(s, &set_data->buf);
            }
        }

        if (sd->desc_pool)
            vk->DestroyDescriptorPool(s->hwctx->act_dev, sd->desc_pool,
                                      s->hwctx->alloc);

        av_freep(&sd->desc_set_buf);
        av_freep(&sd->desc_bind);
        av_freep(&sd->desc_sets);
    }

    av_freep(&pool->reg_shd);

    for (int i = 0; i < pool->pool_size; i++) {
        if (pool->cmd_buf_pools[i])
            vk->FreeCommandBuffers(s->hwctx->act_dev, pool->cmd_buf_pools[i],
                                   1, &pool->cmd_bufs[i]);

        if (pool->cmd_buf_pools[i])
            vk->DestroyCommandPool(s->hwctx->act_dev, pool->cmd_buf_pools[i], s->hwctx->alloc);
    }
    if (pool->query_pool)
        vk->DestroyQueryPool(s->hwctx->act_dev, pool->query_pool, s->hwctx->alloc);

    av_free(pool->query_data);
    av_free(pool->cmd_buf_pools);
    av_free(pool->cmd_bufs);
    av_free(pool->contexts);
}

int ff_vk_exec_pool_init(FFVulkanContext *s, AVVulkanDeviceQueueFamily *qf,
                         FFVkExecPool *pool, int nb_contexts,
                         int nb_queries, VkQueryType query_type, int query_64bit,
                         const void *query_create_pnext)
{
    int err;
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;

    VkCommandPoolCreateInfo cqueue_create;
    VkCommandBufferAllocateInfo cbuf_create;

    const VkQueryPoolVideoEncodeFeedbackCreateInfoKHR *ef = NULL;

    atomic_init(&pool->idx, 0);

    if (query_type == VK_QUERY_TYPE_VIDEO_ENCODE_FEEDBACK_KHR) {
        ef = ff_vk_find_struct(query_create_pnext,
                               VK_STRUCTURE_TYPE_QUERY_POOL_VIDEO_ENCODE_FEEDBACK_CREATE_INFO_KHR);
        if (!ef)
            return AVERROR(EINVAL);
    }

    /* Allocate space for command buffer pools */
    pool->cmd_buf_pools = av_malloc(nb_contexts*sizeof(*pool->cmd_buf_pools));
    if (!pool->cmd_buf_pools) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    /* Allocate space for command buffers */
    pool->cmd_bufs = av_malloc(nb_contexts*sizeof(*pool->cmd_bufs));
    if (!pool->cmd_bufs) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    for (int i = 0; i < nb_contexts; i++) {
        /* Create command pool */
        cqueue_create = (VkCommandPoolCreateInfo) {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags              = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                                  VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex   = qf->idx,
        };

        ret = vk->CreateCommandPool(s->hwctx->act_dev, &cqueue_create,
                                    s->hwctx->alloc, &pool->cmd_buf_pools[i]);
        if (ret != VK_SUCCESS) {
            av_log(s, AV_LOG_ERROR, "Command pool creation failure: %s\n",
                   ff_vk_ret2str(ret));
            err = AVERROR_EXTERNAL;
            goto fail;
        }

        /* Allocate command buffer */
        cbuf_create = (VkCommandBufferAllocateInfo) {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandPool        = pool->cmd_buf_pools[i],
            .commandBufferCount = 1,
        };
        ret = vk->AllocateCommandBuffers(s->hwctx->act_dev, &cbuf_create,
                                         &pool->cmd_bufs[i]);
        if (ret != VK_SUCCESS) {
            av_log(s, AV_LOG_ERROR, "Command buffer alloc failure: %s\n",
                   ff_vk_ret2str(ret));
            err = AVERROR_EXTERNAL;
            goto fail;
        }
    }

    /* Query pool */
    if (nb_queries) {
        VkQueryPoolCreateInfo query_pool_info = {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .pNext = query_create_pnext,
            .queryType = query_type,
            .queryCount = nb_queries*nb_contexts,
        };
        ret = vk->CreateQueryPool(s->hwctx->act_dev, &query_pool_info,
                                  s->hwctx->alloc, &pool->query_pool);
        if (ret != VK_SUCCESS) {
            av_log(s, AV_LOG_ERROR, "Query pool alloc failure: %s\n",
                   ff_vk_ret2str(ret));
            err = AVERROR_EXTERNAL;
            goto fail;
        }

        pool->nb_queries = nb_queries;
        pool->query_status_stride = 1 + 1; /* One result, one status by default */
        pool->query_results = nb_queries;
        pool->query_statuses = nb_queries;

        /* Video encode quieries produce two results per query */
        if (query_type == VK_QUERY_TYPE_VIDEO_ENCODE_FEEDBACK_KHR) {
            int nb_results = av_popcount(ef->encodeFeedbackFlags);
            pool->query_status_stride = nb_results + 1;
            pool->query_results *= nb_results;
        } else if (query_type == VK_QUERY_TYPE_RESULT_STATUS_ONLY_KHR) {
            pool->query_status_stride = 1;
            pool->query_results = 0;
        }

        pool->qd_size = (pool->query_results + pool->query_statuses)*(query_64bit ? 8 : 4);

        /* Allocate space for the query data */
        pool->query_data = av_calloc(nb_contexts, pool->qd_size);
        if (!pool->query_data) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
    }

    /* Allocate space for the contexts */
    pool->contexts = av_calloc(nb_contexts, sizeof(*pool->contexts));
    if (!pool->contexts) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    pool->pool_size = nb_contexts;

    /* Init contexts */
    for (int i = 0; i < pool->pool_size; i++) {
        FFVkExecContext *e = &pool->contexts[i];
        VkFenceCreateInfo fence_create = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };

        /* Fence */
        ret = vk->CreateFence(s->hwctx->act_dev, &fence_create, s->hwctx->alloc,
                              &e->fence);
        if (ret != VK_SUCCESS) {
            av_log(s, AV_LOG_ERROR, "Failed to create submission fence: %s\n",
                   ff_vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }

        e->idx = i;
        e->parent = pool;

        /* Query data */
        e->query_data = ((uint8_t *)pool->query_data) + pool->qd_size*i;
        e->query_idx = nb_queries*i;

        /* Command buffer */
        e->buf = pool->cmd_bufs[i];

        /* Queue index distribution */
        e->qi = i % qf->num;
        e->qf = qf->idx;
        vk->GetDeviceQueue(s->hwctx->act_dev, qf->idx, e->qi, &e->queue);
    }

    return 0;

fail:
    ff_vk_exec_pool_free(s, pool);
    return err;
}

VkResult ff_vk_exec_get_query(FFVulkanContext *s, FFVkExecContext *e,
                              void **data, VkQueryResultFlagBits flags)
{
    FFVulkanFunctions *vk = &s->vkfn;
    const FFVkExecPool *pool = e->parent;
    VkQueryResultFlags qf = flags & ~(VK_QUERY_RESULT_64_BIT |
                                      VK_QUERY_RESULT_WITH_STATUS_BIT_KHR);

    if (!e->query_data) {
        av_log(s, AV_LOG_ERROR, "Requested a query with a NULL query_data pointer!\n");
        return VK_INCOMPLETE;
    }

    qf |= pool->query_64bit ?
          VK_QUERY_RESULT_64_BIT : 0x0;
    qf |= pool->query_statuses ?
          VK_QUERY_RESULT_WITH_STATUS_BIT_KHR : 0x0;

    if (data)
        *data = e->query_data;

    return vk->GetQueryPoolResults(s->hwctx->act_dev, pool->query_pool,
                                   e->query_idx,
                                   pool->nb_queries,
                                   pool->qd_size, e->query_data,
                                   pool->qd_size, qf);
}

FFVkExecContext *ff_vk_exec_get(FFVulkanContext *s, FFVkExecPool *pool)
{
    return &pool->contexts[atomic_fetch_add(&pool->idx, 1) % pool->pool_size];
}

void ff_vk_exec_wait(FFVulkanContext *s, FFVkExecContext *e)
{
    FFVulkanFunctions *vk = &s->vkfn;
    vk->WaitForFences(s->hwctx->act_dev, 1, &e->fence, VK_TRUE, UINT64_MAX);
    ff_vk_exec_discard_deps(s, e);
}

int ff_vk_exec_start(FFVulkanContext *s, FFVkExecContext *e)
{
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;
    const FFVkExecPool *pool = e->parent;

    VkCommandBufferBeginInfo cmd_start = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    /* Wait for the fence to be signalled */
    vk->WaitForFences(s->hwctx->act_dev, 1, &e->fence, VK_TRUE, UINT64_MAX);
    vk->ResetFences(s->hwctx->act_dev, 1, &e->fence);

    /* Discard queue dependencies */
    ff_vk_exec_discard_deps(s, e);

    ret = vk->BeginCommandBuffer(e->buf, &cmd_start);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Failed to start command recoding: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    if (pool->nb_queries)
        vk->CmdResetQueryPool(e->buf, pool->query_pool,
                              e->query_idx, pool->nb_queries);

    return 0;
}

void ff_vk_exec_discard_deps(FFVulkanContext *s, FFVkExecContext *e)
{
    for (int j = 0; j < e->nb_buf_deps; j++)
        av_buffer_unref(&e->buf_deps[j]);
    e->nb_buf_deps = 0;

    for (int j = 0; j < e->nb_sw_frame_deps; j++)
        av_frame_free(&e->sw_frame_deps[j]);
    e->nb_sw_frame_deps = 0;

    for (int j = 0; j < e->nb_frame_deps; j++) {
        AVFrame *f = e->frame_deps[j];
        if (e->frame_locked[j]) {
            AVHWFramesContext *hwfc = (AVHWFramesContext *)f->hw_frames_ctx->data;
            AVVulkanFramesContext *vkfc = hwfc->hwctx;
            AVVkFrame *vkf = (AVVkFrame *)f->data[0];
            vkfc->unlock_frame(hwfc, vkf);
            e->frame_locked[j] = 0;
        }
        e->frame_update[j] = 0;
    }
    e->nb_frame_deps = 0;

    e->sem_wait_cnt = 0;
    e->sem_sig_cnt = 0;
    e->sem_sig_val_dst_cnt = 0;
}

int ff_vk_exec_add_dep_buf(FFVulkanContext *s, FFVkExecContext *e,
                           AVBufferRef **deps, int nb_deps, int ref)
{
    AVBufferRef **dst = av_fast_realloc(e->buf_deps, &e->buf_deps_alloc_size,
                                        (e->nb_buf_deps + nb_deps) * sizeof(*dst));
    if (!dst) {
        ff_vk_exec_discard_deps(s, e);
        return AVERROR(ENOMEM);
    }

    e->buf_deps = dst;

    for (int i = 0; i < nb_deps; i++) {
        if (!deps[i])
            continue;

        e->buf_deps[e->nb_buf_deps] = ref ? av_buffer_ref(deps[i]) : deps[i];
        if (!e->buf_deps[e->nb_buf_deps]) {
            ff_vk_exec_discard_deps(s, e);
            return AVERROR(ENOMEM);
        }
        e->nb_buf_deps++;
    }

    return 0;
}

int ff_vk_exec_add_dep_sw_frame(FFVulkanContext *s, FFVkExecContext *e,
                                AVFrame *f)
{
    AVFrame **dst = av_fast_realloc(e->sw_frame_deps, &e->sw_frame_deps_alloc_size,
                                    (e->nb_sw_frame_deps + 1) * sizeof(*dst));
    if (!dst) {
        ff_vk_exec_discard_deps(s, e);
        return AVERROR(ENOMEM);
    }

    e->sw_frame_deps = dst;

    e->sw_frame_deps[e->nb_sw_frame_deps] = av_frame_clone(f);
    if (!e->sw_frame_deps[e->nb_sw_frame_deps]) {
        ff_vk_exec_discard_deps(s, e);
        return AVERROR(ENOMEM);
    }

    e->nb_sw_frame_deps++;

    return 0;
}

#define ARR_REALLOC(str, arr, alloc_s, cnt)                               \
    do {                                                                  \
        arr = av_fast_realloc(str->arr, alloc_s, (cnt + 1)*sizeof(*arr)); \
        if (!arr) {                                                       \
            ff_vk_exec_discard_deps(s, e);                                \
            return AVERROR(ENOMEM);                                       \
        }                                                                 \
        str->arr = arr;                                                   \
    } while (0)

typedef struct TempSyncCtx {
    int nb_sem;
    VkSemaphore sem[];
} TempSyncCtx;

static void destroy_tmp_semaphores(void *opaque, uint8_t *data)
{
    FFVulkanContext *s = opaque;
    FFVulkanFunctions *vk = &s->vkfn;
    TempSyncCtx *ts = (TempSyncCtx *)data;

    for (int i = 0; i < ts->nb_sem; i++)
        vk->DestroySemaphore(s->hwctx->act_dev, ts->sem[i], s->hwctx->alloc);

    av_free(ts);
}

int ff_vk_exec_add_dep_wait_sem(FFVulkanContext *s, FFVkExecContext *e,
                                VkSemaphore sem, uint64_t val,
                                VkPipelineStageFlagBits2 stage)
{
    VkSemaphoreSubmitInfo *sem_wait;
    ARR_REALLOC(e, sem_wait, &e->sem_wait_alloc, e->sem_wait_cnt);

    e->sem_wait[e->sem_wait_cnt++] = (VkSemaphoreSubmitInfo) {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = sem,
        .value = val,
        .stageMask = stage,
    };

    return 0;
}

int ff_vk_exec_add_dep_bool_sem(FFVulkanContext *s, FFVkExecContext *e,
                                VkSemaphore *sem, int nb,
                                VkPipelineStageFlagBits2 stage,
                                int wait)
{
    int err;
    size_t buf_size;
    AVBufferRef *buf;
    TempSyncCtx *ts;
    FFVulkanFunctions *vk = &s->vkfn;

    /* Do not transfer ownership if we're signalling a binary semaphore,
     * since we're probably exporting it. */
    if (!wait) {
        for (int i = 0; i < nb; i++) {
            VkSemaphoreSubmitInfo *sem_sig;
            ARR_REALLOC(e, sem_sig, &e->sem_sig_alloc, e->sem_sig_cnt);

            e->sem_sig[e->sem_sig_cnt++] = (VkSemaphoreSubmitInfo) {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = sem[i],
                .stageMask = stage,
            };
        }

        return 0;
    }

    buf_size = sizeof(*ts) + sizeof(VkSemaphore)*nb;
    ts = av_mallocz(buf_size);
    if (!ts) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    memcpy(ts->sem, sem, nb*sizeof(*sem));
    ts->nb_sem = nb;

    buf = av_buffer_create((uint8_t *)ts, buf_size, destroy_tmp_semaphores, s, 0);
    if (!buf) {
        av_free(ts);
        err = AVERROR(ENOMEM);
        goto fail;
    }

    err = ff_vk_exec_add_dep_buf(s, e, &buf, 1, 0);
    if (err < 0) {
        av_buffer_unref(&buf);
        return err;
    }

    for (int i = 0; i < nb; i++) {
        err = ff_vk_exec_add_dep_wait_sem(s, e, sem[i], 0, stage);
        if (err < 0)
            return err;
    }

    return 0;

fail:
    for (int i = 0; i < nb; i++)
        vk->DestroySemaphore(s->hwctx->act_dev, sem[i], s->hwctx->alloc);

    return err;
}

int ff_vk_exec_add_dep_frame(FFVulkanContext *s, FFVkExecContext *e, AVFrame *f,
                             VkPipelineStageFlagBits2 wait_stage,
                             VkPipelineStageFlagBits2 signal_stage)
{
    uint8_t *frame_locked;
    uint8_t *frame_update;
    AVFrame **frame_deps;
    AVBufferRef **buf_deps;
    VkImageLayout *layout_dst;
    uint32_t *queue_family_dst;
    VkAccessFlagBits *access_dst;

    AVHWFramesContext *hwfc = (AVHWFramesContext *)f->hw_frames_ctx->data;
    AVVulkanFramesContext *vkfc = hwfc->hwctx;
    AVVkFrame *vkf = (AVVkFrame *)f->data[0];
    int nb_images = ff_vk_count_images(vkf);

    /* Don't add duplicates */
    for (int i = 0; i < e->nb_frame_deps; i++)
        if (e->frame_deps[i]->data[0] == f->data[0])
            return 1;

    ARR_REALLOC(e, layout_dst,       &e->layout_dst_alloc,       e->nb_frame_deps);
    ARR_REALLOC(e, queue_family_dst, &e->queue_family_dst_alloc, e->nb_frame_deps);
    ARR_REALLOC(e, access_dst,       &e->access_dst_alloc,       e->nb_frame_deps);

    ARR_REALLOC(e, frame_locked, &e->frame_locked_alloc_size, e->nb_frame_deps);
    ARR_REALLOC(e, frame_update, &e->frame_update_alloc_size, e->nb_frame_deps);
    ARR_REALLOC(e, frame_deps,   &e->frame_deps_alloc_size,   e->nb_frame_deps);

    /* prepare_frame in hwcontext_vulkan.c uses the regular frame management
     * code but has no frame yet, and it doesn't need to actually store a ref
     * to the frame. */
    if (f->buf[0]) {
        ARR_REALLOC(e, buf_deps, &e->buf_deps_alloc_size, e->nb_buf_deps);
        e->buf_deps[e->nb_buf_deps] = av_buffer_ref(f->buf[0]);
        if (!e->buf_deps[e->nb_buf_deps]) {
            ff_vk_exec_discard_deps(s, e);
            return AVERROR(ENOMEM);
        }
        e->nb_buf_deps++;
    }

    e->frame_deps[e->nb_frame_deps] = f;

    vkfc->lock_frame(hwfc, vkf);
    e->frame_locked[e->nb_frame_deps] = 1;
    e->frame_update[e->nb_frame_deps] = 0;
    e->nb_frame_deps++;

    for (int i = 0; i < nb_images; i++) {
        VkSemaphoreSubmitInfo *sem_wait;
        VkSemaphoreSubmitInfo *sem_sig;
        uint64_t **sem_sig_val_dst;

        ARR_REALLOC(e, sem_wait, &e->sem_wait_alloc, e->sem_wait_cnt);
        ARR_REALLOC(e, sem_sig, &e->sem_sig_alloc, e->sem_sig_cnt);
        ARR_REALLOC(e, sem_sig_val_dst, &e->sem_sig_val_dst_alloc, e->sem_sig_val_dst_cnt);

        e->sem_wait[e->sem_wait_cnt++] = (VkSemaphoreSubmitInfo) {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = vkf->sem[i],
            .value = vkf->sem_value[i],
            .stageMask = wait_stage,
        };

        e->sem_sig[e->sem_sig_cnt++] = (VkSemaphoreSubmitInfo) {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = vkf->sem[i],
            .value = vkf->sem_value[i] + 1,
            .stageMask = signal_stage,
        };

        e->sem_sig_val_dst[e->sem_sig_val_dst_cnt] = &vkf->sem_value[i];
        e->sem_sig_val_dst_cnt++;
    }

    return 0;
}

void ff_vk_exec_update_frame(FFVulkanContext *s, FFVkExecContext *e, AVFrame *f,
                             VkImageMemoryBarrier2 *bar, uint32_t *nb_img_bar)
{
    int i;
    for (i = 0; i < e->nb_frame_deps; i++)
        if (e->frame_deps[i]->data[0] == f->data[0])
            break;
    av_assert0(i < e->nb_frame_deps);

    /* Don't update duplicates */
    if (nb_img_bar && !e->frame_update[i])
        (*nb_img_bar)++;

    e->queue_family_dst[i] = bar->dstQueueFamilyIndex;
    e->access_dst[i] = bar->dstAccessMask;
    e->layout_dst[i] = bar->newLayout;
    e->frame_update[i] = 1;
}

int ff_vk_exec_mirror_sem_value(FFVulkanContext *s, FFVkExecContext *e,
                                VkSemaphore *dst, uint64_t *dst_val,
                                AVFrame *f)
{
    uint64_t **sem_sig_val_dst;
    AVVkFrame *vkf = (AVVkFrame *)f->data[0];

    /* Reject unknown frames */
    int i;
    for (i = 0; i < e->nb_frame_deps; i++)
        if (e->frame_deps[i]->data[0] == f->data[0])
            break;
    if (i == e->nb_frame_deps)
        return AVERROR(EINVAL);

    ARR_REALLOC(e, sem_sig_val_dst, &e->sem_sig_val_dst_alloc, e->sem_sig_val_dst_cnt);

    *dst     = vkf->sem[0];
    *dst_val = vkf->sem_value[0];

    e->sem_sig_val_dst[e->sem_sig_val_dst_cnt] = dst_val;
    e->sem_sig_val_dst_cnt++;

    return 0;
}

int ff_vk_exec_submit(FFVulkanContext *s, FFVkExecContext *e)
{
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;
    VkCommandBufferSubmitInfo cmd_buf_info = (VkCommandBufferSubmitInfo) {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = e->buf,
    };
    VkSubmitInfo2 submit_info = (VkSubmitInfo2) {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .pCommandBufferInfos = &cmd_buf_info,
        .commandBufferInfoCount = 1,
        .pWaitSemaphoreInfos = e->sem_wait,
        .waitSemaphoreInfoCount = e->sem_wait_cnt,
        .pSignalSemaphoreInfos = e->sem_sig,
        .signalSemaphoreInfoCount = e->sem_sig_cnt,
    };

    ret = vk->EndCommandBuffer(e->buf);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Unable to finish command buffer: %s\n",
               ff_vk_ret2str(ret));
        ff_vk_exec_discard_deps(s, e);
        return AVERROR_EXTERNAL;
    }

    s->hwctx->lock_queue(s->device, e->qf, e->qi);
    ret = vk->QueueSubmit2(e->queue, 1, &submit_info, e->fence);
    s->hwctx->unlock_queue(s->device, e->qf, e->qi);

    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Unable to submit command buffer: %s\n",
               ff_vk_ret2str(ret));
        ff_vk_exec_discard_deps(s, e);
        return AVERROR_EXTERNAL;
    }

    for (int i = 0; i < e->sem_sig_val_dst_cnt; i++)
        *e->sem_sig_val_dst[i] += 1;

    /* Unlock all frames */
    for (int j = 0; j < e->nb_frame_deps; j++) {
        if (e->frame_locked[j]) {
            AVFrame *f = e->frame_deps[j];
            AVHWFramesContext *hwfc = (AVHWFramesContext *)f->hw_frames_ctx->data;
            AVVulkanFramesContext *vkfc = hwfc->hwctx;
            AVVkFrame *vkf = (AVVkFrame *)f->data[0];

            if (e->frame_update[j]) {
                int nb_images = ff_vk_count_images(vkf);
                for (int i = 0; i < nb_images; i++) {
                    vkf->layout[i] = e->layout_dst[j];
                    vkf->access[i] = e->access_dst[j];
                    vkf->queue_family[i] = e->queue_family_dst[j];
                }
            }
            vkfc->unlock_frame(hwfc, vkf);
            e->frame_locked[j] = 0;
        }
    }

    e->had_submission = 1;

    return 0;
}

int ff_vk_alloc_mem(FFVulkanContext *s, VkMemoryRequirements *req,
                    VkMemoryPropertyFlagBits req_flags, void *alloc_extension,
                    VkMemoryPropertyFlagBits *mem_flags, VkDeviceMemory *mem)
{
    VkResult ret;
    int index = -1;
    FFVulkanFunctions *vk = &s->vkfn;

    VkMemoryAllocateInfo alloc_info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext           = alloc_extension,
    };

    alloc_info.allocationSize = req->size;

    /* The vulkan spec requires memory types to be sorted in the "optimal"
     * order, so the first matching type we find will be the best/fastest one */
    for (int i = 0; i < s->mprops.memoryTypeCount; i++) {
        /* The memory type must be supported by the requirements (bitfield) */
        if (!(req->memoryTypeBits & (1 << i)))
            continue;

        /* The memory type flags must include our properties */
        if ((req_flags != UINT32_MAX) &&
            ((s->mprops.memoryTypes[i].propertyFlags & req_flags) != req_flags))
            continue;

        /* Found a suitable memory type */
        index = i;
        break;
    }

    if (index < 0) {
        av_log(s, AV_LOG_ERROR, "No memory type found for flags 0x%x\n",
               req_flags);
        return AVERROR(EINVAL);
    }

    alloc_info.memoryTypeIndex = index;

    ret = vk->AllocateMemory(s->hwctx->act_dev, &alloc_info,
                             s->hwctx->alloc, mem);
    if (ret != VK_SUCCESS)
        return AVERROR(ENOMEM);

    if (mem_flags)
        *mem_flags |= s->mprops.memoryTypes[index].propertyFlags;

    return 0;
}

int ff_vk_create_buf(FFVulkanContext *s, FFVkBuffer *buf, size_t size,
                     void *pNext, void *alloc_pNext,
                     VkBufferUsageFlags usage, VkMemoryPropertyFlagBits flags)
{
    int err;
    VkResult ret;
    int use_ded_mem;
    FFVulkanFunctions *vk = &s->vkfn;

    /* Buffer usage flags corresponding to buffer descriptor types */
    const VkBufferUsageFlags desc_usage =
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;

    if ((s->extensions & FF_VK_EXT_DESCRIPTOR_BUFFER) && (usage & desc_usage))
        usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VkBufferCreateInfo buf_spawn = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext       = pNext,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .size        = flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ?
                       FFALIGN(size, s->props.properties.limits.minMemoryMapAlignment) :
                       size,
    };

    VkMemoryAllocateFlagsInfo alloc_flags = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
    };
    VkBufferMemoryRequirementsInfo2 req_desc = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
    };
    VkMemoryDedicatedAllocateInfo ded_alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .pNext = alloc_pNext,
    };
    VkMemoryDedicatedRequirements ded_req = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
    };
    VkMemoryRequirements2 req = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        .pNext = &ded_req,
    };

    av_log(s, AV_LOG_DEBUG, "Creating a buffer of %"SIZE_SPECIFIER" bytes, "
                            "usage: 0x%x, flags: 0x%x\n",
           size, usage, flags);

    ret = vk->CreateBuffer(s->hwctx->act_dev, &buf_spawn, s->hwctx->alloc, &buf->buf);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Failed to create buffer: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    req_desc.buffer = buf->buf;

    vk->GetBufferMemoryRequirements2(s->hwctx->act_dev, &req_desc, &req);

    /* In case the implementation prefers/requires dedicated allocation */
    use_ded_mem = ded_req.prefersDedicatedAllocation |
                  ded_req.requiresDedicatedAllocation;
    if (use_ded_mem) {
        ded_alloc.buffer = buf->buf;
        ded_alloc.pNext = alloc_pNext;
        alloc_pNext = &ded_alloc;
    }

    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        alloc_flags.pNext = alloc_pNext;
        alloc_pNext = &alloc_flags;
    }

    err = ff_vk_alloc_mem(s, &req.memoryRequirements, flags, alloc_pNext,
                          &buf->flags, &buf->mem);
    if (err)
        return err;

    ret = vk->BindBufferMemory(s->hwctx->act_dev, buf->buf, buf->mem, 0);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Failed to bind memory to buffer: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo address_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = buf->buf,
        };
        buf->address = vk->GetBufferDeviceAddress(s->hwctx->act_dev, &address_info);
    }

    buf->size = size;

    return 0;
}

int ff_vk_map_buffers(FFVulkanContext *s, FFVkBuffer **buf, uint8_t *mem[],
                      int nb_buffers, int invalidate)
{
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;
    VkMappedMemoryRange inval_list[64];
    int inval_count = 0;

    for (int i = 0; i < nb_buffers; i++) {
        void *dst;
        ret = vk->MapMemory(s->hwctx->act_dev, buf[i]->mem, 0,
                            VK_WHOLE_SIZE, 0, &dst);
        if (ret != VK_SUCCESS) {
            av_log(s, AV_LOG_ERROR, "Failed to map buffer memory: %s\n",
                   ff_vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }
        mem[i] = buf[i]->mapped_mem = dst;
    }

    if (!invalidate)
        return 0;

    for (int i = 0; i < nb_buffers; i++) {
        const VkMappedMemoryRange ival_buf = {
            .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = buf[i]->mem,
            .size   = VK_WHOLE_SIZE,
        };
        if (buf[i]->flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
            continue;
        inval_list[inval_count++] = ival_buf;
    }

    if (inval_count) {
        ret = vk->InvalidateMappedMemoryRanges(s->hwctx->act_dev, inval_count,
                                               inval_list);
        if (ret != VK_SUCCESS) {
            av_log(s, AV_LOG_ERROR, "Failed to invalidate memory: %s\n",
                   ff_vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }
    }

    return 0;
}

int ff_vk_unmap_buffers(FFVulkanContext *s, FFVkBuffer **buf, int nb_buffers,
                        int flush)
{
    int err = 0;
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;
    VkMappedMemoryRange flush_list[64];
    int flush_count = 0;

    if (flush) {
        for (int i = 0; i < nb_buffers; i++) {
            const VkMappedMemoryRange flush_buf = {
                .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = buf[i]->mem,
                .size   = VK_WHOLE_SIZE,
            };

            av_assert0(!buf[i]->host_ref);
            if (buf[i]->flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                continue;
            flush_list[flush_count++] = flush_buf;
        }
    }

    if (flush_count) {
        ret = vk->FlushMappedMemoryRanges(s->hwctx->act_dev, flush_count,
                                          flush_list);
        if (ret != VK_SUCCESS) {
            av_log(s, AV_LOG_ERROR, "Failed to flush memory: %s\n",
                   ff_vk_ret2str(ret));
            err = AVERROR_EXTERNAL; /* We still want to try to unmap them */
        }
    }

    for (int i = 0; i < nb_buffers; i++) {
        vk->UnmapMemory(s->hwctx->act_dev, buf[i]->mem);
        buf[i]->mapped_mem = NULL;
    }

    return err;
}

void ff_vk_free_buf(FFVulkanContext *s, FFVkBuffer *buf)
{
    FFVulkanFunctions *vk = &s->vkfn;

    if (!buf || !s->hwctx)
        return;

    if (buf->mapped_mem && !buf->host_ref)
        ff_vk_unmap_buffer(s, buf, 0);
    if (buf->buf != VK_NULL_HANDLE)
        vk->DestroyBuffer(s->hwctx->act_dev, buf->buf, s->hwctx->alloc);
    if (buf->mem != VK_NULL_HANDLE)
        vk->FreeMemory(s->hwctx->act_dev, buf->mem, s->hwctx->alloc);
    if (buf->host_ref)
        av_buffer_unref(&buf->host_ref);

    buf->buf = VK_NULL_HANDLE;
    buf->mem = VK_NULL_HANDLE;
    buf->mapped_mem = NULL;
}

static void free_data_buf(void *opaque, uint8_t *data)
{
    FFVulkanContext *ctx = opaque;
    FFVkBuffer *buf = (FFVkBuffer *)data;
    ff_vk_free_buf(ctx, buf);
    av_free(data);
}

static AVBufferRef *alloc_data_buf(void *opaque, size_t size)
{
    AVBufferRef *ref;
    uint8_t *buf = av_mallocz(size);
    if (!buf)
        return NULL;

    ref = av_buffer_create(buf, size, free_data_buf, opaque, 0);
    if (!ref)
        av_free(buf);
    return ref;
}

int ff_vk_get_pooled_buffer(FFVulkanContext *ctx, AVBufferPool **buf_pool,
                            AVBufferRef **buf, VkBufferUsageFlags usage,
                            void *create_pNext, size_t size,
                            VkMemoryPropertyFlagBits mem_props)
{
    int err;
    AVBufferRef *ref;
    FFVkBuffer *data;

    *buf = NULL;

    if (!(*buf_pool)) {
        *buf_pool = av_buffer_pool_init2(sizeof(FFVkBuffer), ctx,
                                         alloc_data_buf, NULL);
        if (!(*buf_pool))
            return AVERROR(ENOMEM);
    }

    *buf = ref = av_buffer_pool_get(*buf_pool);
    if (!ref)
        return AVERROR(ENOMEM);

    data = (FFVkBuffer *)ref->data;
    data->stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    data->access = VK_ACCESS_2_NONE;

    if (data->size >= size)
        return 0;

    ff_vk_free_buf(ctx, data);
    memset(data, 0, sizeof(*data));

    err = ff_vk_create_buf(ctx, data, size,
                           create_pNext, NULL, usage,
                           mem_props);
    if (err < 0) {
        av_buffer_unref(&ref);
        *buf = NULL;
        return err;
    }

    if (mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        err = ff_vk_map_buffer(ctx, data, &data->mapped_mem, 0);
        if (err < 0) {
            av_buffer_unref(&ref);
            *buf = NULL;
            return err;
        }
    }

    return 0;
}

static int create_mapped_buffer(FFVulkanContext *s,
                                FFVkBuffer *vkb, VkBufferUsageFlags usage,
                                size_t size,
                                VkExternalMemoryBufferCreateInfo *create_desc,
                                VkImportMemoryHostPointerInfoEXT *import_desc,
                                VkMemoryHostPointerPropertiesEXT props)
{
    int err;
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;

    VkBufferCreateInfo buf_spawn = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext       = create_desc,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .size        = size,
    };
    VkMemoryRequirements req = {
        .size           = size,
        .alignment      = s->hprops.minImportedHostPointerAlignment,
        .memoryTypeBits = props.memoryTypeBits,
    };

    err = ff_vk_alloc_mem(s, &req,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                          import_desc, &vkb->flags, &vkb->mem);
    if (err < 0)
        return err;

    ret = vk->CreateBuffer(s->hwctx->act_dev, &buf_spawn, s->hwctx->alloc, &vkb->buf);
    if (ret != VK_SUCCESS) {
        vk->FreeMemory(s->hwctx->act_dev, vkb->mem, s->hwctx->alloc);
        return AVERROR_EXTERNAL;
    }

    ret = vk->BindBufferMemory(s->hwctx->act_dev, vkb->buf, vkb->mem, 0);
    if (ret != VK_SUCCESS) {
        vk->FreeMemory(s->hwctx->act_dev, vkb->mem, s->hwctx->alloc);
        vk->DestroyBuffer(s->hwctx->act_dev, vkb->buf, s->hwctx->alloc);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static void destroy_avvkbuf(void *opaque, uint8_t *data)
{
    FFVulkanContext *s = opaque;
    FFVkBuffer *buf = (FFVkBuffer *)data;
    ff_vk_free_buf(s, buf);
    av_free(buf);
}

int ff_vk_host_map_buffer(FFVulkanContext *s, AVBufferRef **dst,
                          uint8_t *src_data, const AVBufferRef *src_buf,
                          VkBufferUsageFlags usage)
{
    int err;
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;

    VkExternalMemoryBufferCreateInfo create_desc = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
    };
    VkMemoryAllocateFlagsInfo alloc_flags = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
    };
    VkImportMemoryHostPointerInfoEXT import_desc = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
        .pNext = usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ? &alloc_flags : NULL,
    };
    VkMemoryHostPointerPropertiesEXT props;

    AVBufferRef *ref;
    FFVkBuffer *vkb;
    size_t offs;
    size_t buffer_size;

    *dst = NULL;

    /* Get the previous point at which mapping was possible and use it */
    offs = (uintptr_t)src_data % s->hprops.minImportedHostPointerAlignment;
    import_desc.pHostPointer = src_data - offs;

    props = (VkMemoryHostPointerPropertiesEXT) {
        VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT,
    };
    ret = vk->GetMemoryHostPointerPropertiesEXT(s->hwctx->act_dev,
                                                import_desc.handleType,
                                                import_desc.pHostPointer,
                                                &props);
    if (!(ret == VK_SUCCESS && props.memoryTypeBits))
        return AVERROR(EINVAL);

    /* Ref the source buffer */
    ref = av_buffer_ref(src_buf);
    if (!ref)
        return AVERROR(ENOMEM);

    /* Add the offset at the start, which gets ignored */
    buffer_size = offs + src_buf->size;
    buffer_size = FFALIGN(buffer_size, s->props.properties.limits.minMemoryMapAlignment);
    buffer_size = FFALIGN(buffer_size, s->hprops.minImportedHostPointerAlignment);

    /* Create a buffer struct */
    vkb = av_mallocz(sizeof(*vkb));
    if (!vkb) {
        av_buffer_unref(&ref);
        return AVERROR(ENOMEM);
    }

    err = create_mapped_buffer(s, vkb, usage,
                               buffer_size, &create_desc, &import_desc,
                               props);
    if (err < 0) {
        av_buffer_unref(&ref);
        av_free(vkb);
        return err;
    }

    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo address_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = vkb->buf,
        };
        vkb->address = vk->GetBufferDeviceAddress(s->hwctx->act_dev, &address_info);
    }

    vkb->host_ref       = ref;
    vkb->virtual_offset = offs;
    vkb->address       += offs;
    vkb->mapped_mem     = src_data;
    vkb->size           = buffer_size - offs;
    vkb->flags         |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    /* Create a ref */
    *dst = av_buffer_create((uint8_t *)vkb, sizeof(*vkb),
                            destroy_avvkbuf, s, 0);
    if (!(*dst)) {
        destroy_avvkbuf(s, (uint8_t *)vkb);
        *dst = NULL;
        return AVERROR(ENOMEM);
    }

    return 0;
}

int ff_vk_shader_add_push_const(FFVulkanShader *shd, int offset, int size,
                                VkShaderStageFlagBits stage)
{
    VkPushConstantRange *pc;

    shd->push_consts = av_realloc_array(shd->push_consts,
                                        sizeof(*shd->push_consts),
                                        shd->push_consts_num + 1);
    if (!shd->push_consts)
        return AVERROR(ENOMEM);

    pc = &shd->push_consts[shd->push_consts_num++];
    memset(pc, 0, sizeof(*pc));

    pc->stageFlags = stage;
    pc->offset = offset;
    pc->size = size;

    return 0;
}

int ff_vk_init_sampler(FFVulkanContext *s, VkSampler *sampler,
                       int unnorm_coords, VkFilter filt)
{
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;

    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = filt,
        .minFilter = sampler_info.magFilter,
        .mipmapMode = unnorm_coords ? VK_SAMPLER_MIPMAP_MODE_NEAREST :
                                      VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = sampler_info.addressModeU,
        .addressModeW = sampler_info.addressModeU,
        .anisotropyEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_NEVER,
        .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        .unnormalizedCoordinates = unnorm_coords,
    };

    ret = vk->CreateSampler(s->hwctx->act_dev, &sampler_info,
                            s->hwctx->alloc, sampler);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Unable to init sampler: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    return 0;
}

VkImageAspectFlags ff_vk_aspect_flag(AVFrame *f, int p)
{
    AVVkFrame *vkf = (AVVkFrame *)f->data[0];
    AVHWFramesContext *hwfc = (AVHWFramesContext *)f->hw_frames_ctx->data;
    int nb_images = ff_vk_count_images(vkf);
    int nb_planes = av_pix_fmt_count_planes(hwfc->sw_format);

    static const VkImageAspectFlags plane_aspect[] = { VK_IMAGE_ASPECT_PLANE_0_BIT,
                                                       VK_IMAGE_ASPECT_PLANE_1_BIT,
                                                       VK_IMAGE_ASPECT_PLANE_2_BIT, };

    if (ff_vk_mt_is_np_rgb(hwfc->sw_format) || (nb_planes == nb_images))
        return VK_IMAGE_ASPECT_COLOR_BIT;

    return plane_aspect[p];
}

int ff_vk_mt_is_np_rgb(enum AVPixelFormat pix_fmt)
{
    if (pix_fmt == AV_PIX_FMT_ABGR   || pix_fmt == AV_PIX_FMT_BGRA   ||
        pix_fmt == AV_PIX_FMT_RGBA   || pix_fmt == AV_PIX_FMT_RGB24  ||
        pix_fmt == AV_PIX_FMT_BGR24  || pix_fmt == AV_PIX_FMT_RGB48  ||
        pix_fmt == AV_PIX_FMT_RGBA64 || pix_fmt == AV_PIX_FMT_RGB565 ||
        pix_fmt == AV_PIX_FMT_BGR565 || pix_fmt == AV_PIX_FMT_BGR0   ||
        pix_fmt == AV_PIX_FMT_0BGR   || pix_fmt == AV_PIX_FMT_RGB0   ||
        pix_fmt == AV_PIX_FMT_GBRP10  || pix_fmt == AV_PIX_FMT_GBRP12 ||
        pix_fmt == AV_PIX_FMT_GBRP14  || pix_fmt == AV_PIX_FMT_GBRP16 ||
        pix_fmt == AV_PIX_FMT_GBRAP   || pix_fmt == AV_PIX_FMT_GBRAP10 ||
        pix_fmt == AV_PIX_FMT_GBRAP12 || pix_fmt == AV_PIX_FMT_GBRAP14 ||
        pix_fmt == AV_PIX_FMT_GBRAP16 || pix_fmt == AV_PIX_FMT_GBRAP32 ||
        pix_fmt == AV_PIX_FMT_GBRPF32 || pix_fmt == AV_PIX_FMT_GBRAPF32 ||
        pix_fmt == AV_PIX_FMT_X2RGB10 || pix_fmt == AV_PIX_FMT_X2BGR10 ||
        pix_fmt == AV_PIX_FMT_RGBAF32 || pix_fmt == AV_PIX_FMT_RGBF32 ||
        pix_fmt == AV_PIX_FMT_RGBA128 || pix_fmt == AV_PIX_FMT_RGB96 ||
        pix_fmt == AV_PIX_FMT_GBRP)
        return 1;
    return 0;
}

void ff_vk_set_perm(enum AVPixelFormat pix_fmt, int lut[4], int inv)
{
    switch (pix_fmt) {
    case AV_PIX_FMT_GBRP:
    case AV_PIX_FMT_GBRAP:
    case AV_PIX_FMT_GBRAP10:
    case AV_PIX_FMT_GBRAP12:
    case AV_PIX_FMT_GBRAP14:
    case AV_PIX_FMT_GBRAP16:
    case AV_PIX_FMT_GBRP10:
    case AV_PIX_FMT_GBRP12:
    case AV_PIX_FMT_GBRP14:
    case AV_PIX_FMT_GBRP16:
    case AV_PIX_FMT_GBRPF32:
    case AV_PIX_FMT_GBRAP32:
    case AV_PIX_FMT_GBRAPF32:
        lut[0] = 1;
        lut[1] = 2;
        lut[2] = 0;
        lut[3] = 3;
        break;
    default:
        lut[0] = 0;
        lut[1] = 1;
        lut[2] = 2;
        lut[3] = 3;
        break;
    }

    if (inv) {
        int lut_tmp[4] = { lut[0], lut[1], lut[2], lut[3] };
        for (int i = 0; i < 4; i++)
            lut[lut_tmp[i]] = i;
    }

    return;
}

const char *ff_vk_shader_rep_fmt(enum AVPixelFormat pix_fmt,
                                 enum FFVkShaderRepFormat rep_fmt)
{
    switch (pix_fmt) {
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_BGRA:
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_BGR24:
    case AV_PIX_FMT_BGR0:
    case AV_PIX_FMT_RGB0:
    case AV_PIX_FMT_RGB565:
    case AV_PIX_FMT_BGR565:
    case AV_PIX_FMT_UYVA:
    case AV_PIX_FMT_YUYV422:
    case AV_PIX_FMT_UYVY422: {
        const char *rep_tab[] = {
            [FF_VK_REP_NATIVE] = "rgba8ui",
            [FF_VK_REP_FLOAT] = "rgba8",
            [FF_VK_REP_INT] = "rgba8i",
            [FF_VK_REP_UINT] = "rgba8ui",
        };
        return rep_tab[rep_fmt];
    }
    case AV_PIX_FMT_X2RGB10:
    case AV_PIX_FMT_X2BGR10:
    case AV_PIX_FMT_Y210:
    case AV_PIX_FMT_XV30: {
        const char *rep_tab[] = {
            [FF_VK_REP_NATIVE] = "rgb10_a2ui",
            [FF_VK_REP_FLOAT] = "rgb10_a2",
            [FF_VK_REP_INT] = NULL,
            [FF_VK_REP_UINT] = "rgb10_a2ui",
        };
        return rep_tab[rep_fmt];
    }
    case AV_PIX_FMT_RGB48:
    case AV_PIX_FMT_RGBA64:
    case AV_PIX_FMT_Y212:
    case AV_PIX_FMT_Y216:
    case AV_PIX_FMT_XV36:
    case AV_PIX_FMT_XV48: {
        const char *rep_tab[] = {
            [FF_VK_REP_NATIVE] = "rgba16ui",
            [FF_VK_REP_FLOAT] = "rgba16",
            [FF_VK_REP_INT] = "rgba16i",
            [FF_VK_REP_UINT] = "rgba16ui",
        };
        return rep_tab[rep_fmt];
    }
    case AV_PIX_FMT_RGBF32:
    case AV_PIX_FMT_RGBAF32: {
        const char *rep_tab[] = {
            [FF_VK_REP_NATIVE] = "rgba32f",
            [FF_VK_REP_FLOAT] = "rgba32f",
            [FF_VK_REP_INT] = "rgba32i",
            [FF_VK_REP_UINT] = "rgba32ui",
        };
        return rep_tab[rep_fmt];
    }
    case AV_PIX_FMT_RGB96:
    case AV_PIX_FMT_RGBA128: {
        const char *rep_tab[] = {
            [FF_VK_REP_NATIVE] = "rgba32ui",
            [FF_VK_REP_FLOAT] = NULL,
            [FF_VK_REP_INT] = "rgba32i",
            [FF_VK_REP_UINT] = "rgba32ui",
        };
        return rep_tab[rep_fmt];
    }
    case AV_PIX_FMT_GBRP:
    case AV_PIX_FMT_GRAY8:
    case AV_PIX_FMT_GBRAP:
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVA420P:
    case AV_PIX_FMT_YUVA422P:
    case AV_PIX_FMT_YUVA444P: {
        const char *rep_tab[] = {
            [FF_VK_REP_NATIVE] = "r8ui",
            [FF_VK_REP_FLOAT] = "r8",
            [FF_VK_REP_INT] = "r8i",
            [FF_VK_REP_UINT] = "r8ui",
        };
        return rep_tab[rep_fmt];
    };
    case AV_PIX_FMT_GRAY10:
    case AV_PIX_FMT_GRAY12:
    case AV_PIX_FMT_GRAY14:
    case AV_PIX_FMT_GRAY16:
    case AV_PIX_FMT_GBRAP10:
    case AV_PIX_FMT_GBRAP12:
    case AV_PIX_FMT_GBRAP14:
    case AV_PIX_FMT_GBRAP16:
    case AV_PIX_FMT_GBRP10:
    case AV_PIX_FMT_GBRP12:
    case AV_PIX_FMT_GBRP14:
    case AV_PIX_FMT_GBRP16:
    case AV_PIX_FMT_YUV420P10:
    case AV_PIX_FMT_YUV420P12:
    case AV_PIX_FMT_YUV420P16:
    case AV_PIX_FMT_YUV422P10:
    case AV_PIX_FMT_YUV422P12:
    case AV_PIX_FMT_YUV422P16:
    case AV_PIX_FMT_YUV444P10:
    case AV_PIX_FMT_YUV444P12:
    case AV_PIX_FMT_YUV444P16:
    case AV_PIX_FMT_YUVA420P10:
    case AV_PIX_FMT_YUVA420P16:
    case AV_PIX_FMT_YUVA422P10:
    case AV_PIX_FMT_YUVA422P12:
    case AV_PIX_FMT_YUVA422P16:
    case AV_PIX_FMT_YUVA444P10:
    case AV_PIX_FMT_YUVA444P12:
    case AV_PIX_FMT_YUVA444P16: {
        const char *rep_tab[] = {
            [FF_VK_REP_NATIVE] = "r16ui",
            [FF_VK_REP_FLOAT] = "r16f",
            [FF_VK_REP_INT] = "r16i",
            [FF_VK_REP_UINT] = "r16ui",
        };
        return rep_tab[rep_fmt];
    };
    case AV_PIX_FMT_GRAY32:
    case AV_PIX_FMT_GRAYF32:
    case AV_PIX_FMT_GBRPF32:
    case AV_PIX_FMT_GBRAPF32: {
        const char *rep_tab[] = {
            [FF_VK_REP_NATIVE] = "r32f",
            [FF_VK_REP_FLOAT] = "r32f",
            [FF_VK_REP_INT] = "r32i",
            [FF_VK_REP_UINT] = "r32ui",
        };
        return rep_tab[rep_fmt];
    };
    case AV_PIX_FMT_GBRAP32: {
        const char *rep_tab[] = {
            [FF_VK_REP_NATIVE] = "r32ui",
            [FF_VK_REP_FLOAT] = NULL,
            [FF_VK_REP_INT] = "r32i",
            [FF_VK_REP_UINT] = "r32ui",
        };
        return rep_tab[rep_fmt];
    };
    case AV_PIX_FMT_NV12:
    case AV_PIX_FMT_NV16:
    case AV_PIX_FMT_NV24: {
        const char *rep_tab[] = {
            [FF_VK_REP_NATIVE] = "rg8ui",
            [FF_VK_REP_FLOAT] = "rg8",
            [FF_VK_REP_INT] = "rg8i",
            [FF_VK_REP_UINT] = "rg8ui",
        };
        return rep_tab[rep_fmt];
    };
    case AV_PIX_FMT_P010:
    case AV_PIX_FMT_P210:
    case AV_PIX_FMT_P410: {
        const char *rep_tab[] = {
            [FF_VK_REP_NATIVE] = "rgb10_a2ui",
            [FF_VK_REP_FLOAT] = "rgb10_a2",
            [FF_VK_REP_INT] = NULL,
            [FF_VK_REP_UINT] = "rgb10_a2ui",
        };
        return rep_tab[rep_fmt];
    };
    case AV_PIX_FMT_P012:
    case AV_PIX_FMT_P016:
    case AV_PIX_FMT_P212:
    case AV_PIX_FMT_P216:
    case AV_PIX_FMT_P412:
    case AV_PIX_FMT_P416: {
        const char *rep_tab[] = {
            [FF_VK_REP_NATIVE] = "rg16ui",
            [FF_VK_REP_FLOAT] = "rg16",
            [FF_VK_REP_INT] = "rg16i",
            [FF_VK_REP_UINT] = "rg16ui",
        };
        return rep_tab[rep_fmt];
    };
    default:
        return "rgba32f";
    }
}

typedef struct ImageViewCtx {
    int nb_views;
    VkImageView views[];
} ImageViewCtx;

static void destroy_imageviews(void *opaque, uint8_t *data)
{
    FFVulkanContext *s = opaque;
    FFVulkanFunctions *vk = &s->vkfn;
    ImageViewCtx *iv = (ImageViewCtx *)data;

    for (int i = 0; i < iv->nb_views; i++)
        vk->DestroyImageView(s->hwctx->act_dev, iv->views[i], s->hwctx->alloc);

    av_free(iv);
}

static VkFormat map_fmt_to_rep(VkFormat fmt, enum FFVkShaderRepFormat rep_fmt)
{
#define REPS_FMT(fmt) \
    [FF_VK_REP_NATIVE] = fmt ## _UINT, \
    [FF_VK_REP_FLOAT]  = fmt ## _UNORM, \
    [FF_VK_REP_INT]    = fmt ## _SINT, \
    [FF_VK_REP_UINT]   = fmt ## _UINT,

#define REPS_FMT_PACK(fmt, num) \
    [FF_VK_REP_NATIVE] = fmt ## _UINT_PACK ## num, \
    [FF_VK_REP_FLOAT]  = fmt ## _UNORM_PACK ## num, \
    [FF_VK_REP_INT]    = fmt ## _SINT_PACK ## num, \
    [FF_VK_REP_UINT]   = fmt ## _UINT_PACK ## num,

    const VkFormat fmts_map[][4] = {
        { REPS_FMT_PACK(VK_FORMAT_A2B10G10R10, 32) },
        { REPS_FMT_PACK(VK_FORMAT_A2R10G10B10, 32) },
        {
            VK_FORMAT_B5G6R5_UNORM_PACK16,
            VK_FORMAT_B5G6R5_UNORM_PACK16,
            VK_FORMAT_UNDEFINED,
            VK_FORMAT_UNDEFINED,
        },
        {
            VK_FORMAT_R5G6B5_UNORM_PACK16,
            VK_FORMAT_R5G6B5_UNORM_PACK16,
            VK_FORMAT_UNDEFINED,
            VK_FORMAT_UNDEFINED,
        },
        { REPS_FMT(VK_FORMAT_B8G8R8) },
        { REPS_FMT(VK_FORMAT_B8G8R8A8) },
        { REPS_FMT(VK_FORMAT_R8) },
        { REPS_FMT(VK_FORMAT_R8G8) },
        { REPS_FMT(VK_FORMAT_R8G8B8) },
        { REPS_FMT(VK_FORMAT_R8G8B8A8) },
        { REPS_FMT(VK_FORMAT_R16) },
        { REPS_FMT(VK_FORMAT_R16G16) },
        { REPS_FMT(VK_FORMAT_R16G16B16) },
        { REPS_FMT(VK_FORMAT_R16G16B16A16) },
        {
            VK_FORMAT_R32_UINT,
            VK_FORMAT_R32_SFLOAT,
            VK_FORMAT_R32_SINT,
            VK_FORMAT_R32_UINT,
        },
        {
            VK_FORMAT_R32G32B32_SFLOAT,
            VK_FORMAT_R32G32B32_SFLOAT,
            VK_FORMAT_UNDEFINED,
            VK_FORMAT_UNDEFINED,
        },
        {
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_FORMAT_UNDEFINED,
            VK_FORMAT_UNDEFINED,
        },
        {
            VK_FORMAT_R32G32B32_UINT,
            VK_FORMAT_UNDEFINED,
            VK_FORMAT_R32G32B32_SINT,
            VK_FORMAT_R32G32B32_UINT,
        },
        {
            VK_FORMAT_R32G32B32A32_UINT,
            VK_FORMAT_UNDEFINED,
            VK_FORMAT_R32G32B32A32_SINT,
            VK_FORMAT_R32G32B32A32_UINT,
        },
    };
#undef REPS_FMT_PACK
#undef REPS_FMT

    if (fmt == VK_FORMAT_UNDEFINED)
        return VK_FORMAT_UNDEFINED;

    for (int i = 0; i < FF_ARRAY_ELEMS(fmts_map); i++) {
        if (fmts_map[i][FF_VK_REP_NATIVE] == fmt ||
            fmts_map[i][FF_VK_REP_FLOAT] == fmt ||
            fmts_map[i][FF_VK_REP_INT] == fmt ||
            fmts_map[i][FF_VK_REP_UINT] == fmt)
            return fmts_map[i][rep_fmt];
    }

    return VK_FORMAT_UNDEFINED;
}

int ff_vk_create_imageview(FFVulkanContext *s,
                           VkImageView *img_view, VkImageAspectFlags *aspect,
                           AVFrame *f, int plane, enum FFVkShaderRepFormat rep_fmt)
{
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;
    AVHWFramesContext *hwfc = (AVHWFramesContext *)f->hw_frames_ctx->data;
    AVVulkanFramesContext *vkfc = hwfc->hwctx;
    const VkFormat *rep_fmts = av_vkfmt_from_pixfmt(hwfc->sw_format);
    AVVkFrame *vkf = (AVVkFrame *)f->data[0];
    const int nb_images = ff_vk_count_images(vkf);

    VkImageViewUsageCreateInfo view_usage_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
        .usage = vkfc->usage &
                 (~(VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR |
                    VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR)),
    };
    VkImageViewCreateInfo view_create_info = {
        .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext      = &view_usage_info,
        .image      = vkf->img[FFMIN(plane, nb_images - 1)],
        .viewType   = VK_IMAGE_VIEW_TYPE_2D,
        .format     = map_fmt_to_rep(rep_fmts[plane], rep_fmt),
        .components = ff_comp_identity_map,
        .subresourceRange = {
            .aspectMask = ff_vk_aspect_flag(f, plane),
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    if (view_create_info.format == VK_FORMAT_UNDEFINED) {
        av_log(s, AV_LOG_ERROR, "Unable to find a compatible representation "
                                "of format %i and mode %i\n",
               rep_fmts[plane], rep_fmt);
        return AVERROR(EINVAL);
    }

    ret = vk->CreateImageView(s->hwctx->act_dev, &view_create_info,
                              s->hwctx->alloc, img_view);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Failed to create imageview: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    *aspect = view_create_info.subresourceRange.aspectMask;

    return 0;
}

int ff_vk_create_imageviews(FFVulkanContext *s, FFVkExecContext *e,
                            VkImageView views[AV_NUM_DATA_POINTERS],
                            AVFrame *f, enum FFVkShaderRepFormat rep_fmt)
{
    int err;
    VkResult ret;
    AVBufferRef *buf;
    FFVulkanFunctions *vk = &s->vkfn;
    AVHWFramesContext *hwfc = (AVHWFramesContext *)f->hw_frames_ctx->data;
    AVVulkanFramesContext *vkfc = hwfc->hwctx;
    const VkFormat *rep_fmts = av_vkfmt_from_pixfmt(hwfc->sw_format);
    AVVkFrame *vkf = (AVVkFrame *)f->data[0];
    const int nb_images = ff_vk_count_images(vkf);
    const int nb_planes = av_pix_fmt_count_planes(hwfc->sw_format);

    ImageViewCtx *iv;
    const size_t buf_size = sizeof(*iv) + nb_planes*sizeof(VkImageView);
    iv = av_mallocz(buf_size);
    if (!iv)
        return AVERROR(ENOMEM);

    for (int i = 0; i < nb_planes; i++) {
        VkImageViewUsageCreateInfo view_usage_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
            .usage = vkfc->usage &
                     (~(VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR |
                        VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR)),
        };
        VkImageViewCreateInfo view_create_info = {
            .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext      = &view_usage_info,
            .image      = vkf->img[FFMIN(i, nb_images - 1)],
            .viewType   = VK_IMAGE_VIEW_TYPE_2D,
            .format     = map_fmt_to_rep(rep_fmts[i], rep_fmt),
            .components = ff_comp_identity_map,
            .subresourceRange = {
                .aspectMask = ff_vk_aspect_flag(f, i),
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        if (view_create_info.format == VK_FORMAT_UNDEFINED) {
            av_log(s, AV_LOG_ERROR, "Unable to find a compatible representation "
                                    "of format %i and mode %i\n",
                   rep_fmts[i], rep_fmt);
            err = AVERROR(EINVAL);
            goto fail;
        }

        ret = vk->CreateImageView(s->hwctx->act_dev, &view_create_info,
                                  s->hwctx->alloc, &iv->views[i]);
        if (ret != VK_SUCCESS) {
            av_log(s, AV_LOG_ERROR, "Failed to create imageview: %s\n",
                   ff_vk_ret2str(ret));
            err = AVERROR_EXTERNAL;
            goto fail;
        }

        iv->nb_views++;
    }

    buf = av_buffer_create((uint8_t *)iv, buf_size, destroy_imageviews, s, 0);
    if (!buf) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    /* Add to queue dependencies */
    err = ff_vk_exec_add_dep_buf(s, e, &buf, 1, 0);
    if (err < 0)
        av_buffer_unref(&buf);

    memcpy(views, iv->views, nb_planes*sizeof(*views));

    return err;

fail:
    for (int i = 0; i < iv->nb_views; i++)
        vk->DestroyImageView(s->hwctx->act_dev, iv->views[i], s->hwctx->alloc);
    av_free(iv);
    return err;
}

void ff_vk_frame_barrier(FFVulkanContext *s, FFVkExecContext *e,
                         AVFrame *pic, VkImageMemoryBarrier2 *bar, int *nb_bar,
                         VkPipelineStageFlags src_stage,
                         VkPipelineStageFlags dst_stage,
                         VkAccessFlagBits     new_access,
                         VkImageLayout        new_layout,
                         uint32_t             new_qf)
{
    int found = -1;
    AVVkFrame *vkf = (AVVkFrame *)pic->data[0];
    const int nb_images = ff_vk_count_images(vkf);
    for (int i = 0; i < e->nb_frame_deps; i++)
        if (e->frame_deps[i]->data[0] == pic->data[0]) {
            if (e->frame_update[i])
                found = i;
            break;
        }

    for (int i = 0; i < nb_images; i++) {
        bar[*nb_bar] = (VkImageMemoryBarrier2) {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = src_stage,
            .dstStageMask = dst_stage,
            .srcAccessMask = found >= 0 ? e->access_dst[found] : vkf->access[i],
            .dstAccessMask = new_access,
            .oldLayout = found >= 0 ? e->layout_dst[found] : vkf->layout[0],
            .newLayout = new_layout,
            .srcQueueFamilyIndex = found >= 0 ? e->queue_family_dst[found] : vkf->queue_family[0],
            .dstQueueFamilyIndex = new_qf,
            .image = vkf->img[i],
            .subresourceRange = (VkImageSubresourceRange) {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1,
                .levelCount = 1,
            },
        };
        *nb_bar += 1;
    }

    ff_vk_exec_update_frame(s, e, pic, &bar[*nb_bar - nb_images], NULL);
}

int ff_vk_shader_init(FFVulkanContext *s, FFVulkanShader *shd, const char *name,
                      VkPipelineStageFlags stage,
                      const char *extensions[], int nb_extensions,
                      int lg_x, int lg_y, int lg_z,
                      uint32_t required_subgroup_size)
{
    av_bprint_init(&shd->src, 0, AV_BPRINT_SIZE_UNLIMITED);

    shd->name = name;
    shd->stage = stage;
    shd->lg_size[0] = lg_x;
    shd->lg_size[1] = lg_y;
    shd->lg_size[2] = lg_z;

    switch (shd->stage) {
    case VK_SHADER_STAGE_ANY_HIT_BIT_KHR:
    case VK_SHADER_STAGE_CALLABLE_BIT_KHR:
    case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
    case VK_SHADER_STAGE_INTERSECTION_BIT_KHR:
    case VK_SHADER_STAGE_MISS_BIT_KHR:
    case VK_SHADER_STAGE_RAYGEN_BIT_KHR:
        shd->bind_point = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
        break;
    case VK_SHADER_STAGE_COMPUTE_BIT:
        shd->bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
        break;
    default:
        shd->bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
        break;
    };

    if (required_subgroup_size) {
        shd->subgroup_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO;
        shd->subgroup_info.requiredSubgroupSize = required_subgroup_size;
    }

    av_bprintf(&shd->src, "/* %s shader: %s */\n",
               (stage == VK_SHADER_STAGE_TASK_BIT_EXT ||
                stage == VK_SHADER_STAGE_MESH_BIT_EXT) ?
               "Mesh" :
               (shd->bind_point == VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR) ?
               "Raytrace" :
               (shd->bind_point == VK_PIPELINE_BIND_POINT_COMPUTE) ?
               "Compute" : "Graphics",
               name);
    GLSLF(0, #version %i                                                  ,460);
    GLSLC(0,                                                                  );

    /* Common utilities */
    GLSLC(0, #define IS_WITHIN(v1, v2) ((v1.x < v2.x) && (v1.y < v2.y))       );
    GLSLC(0,                                                                  );
    GLSLC(0, #extension GL_EXT_scalar_block_layout : require                  );
    GLSLC(0, #extension GL_EXT_shader_explicit_arithmetic_types : require     );
    GLSLC(0, #extension GL_EXT_control_flow_attributes : require              );
    GLSLC(0, #extension GL_EXT_shader_image_load_formatted : require          );
    if (s->extensions & FF_VK_EXT_EXPECT_ASSUME) {
        GLSLC(0, #extension GL_EXT_expect_assume : require                    );
    } else {
        GLSLC(0, #define assumeEXT(x) (x)                                     );
        GLSLC(0, #define expectEXT(x, c) (x)                                  );
    }
    if ((s->extensions & FF_VK_EXT_DEBUG_UTILS) &&
        (s->extensions & FF_VK_EXT_RELAXED_EXTENDED_INSTR)) {
        GLSLC(0, #extension GL_EXT_debug_printf : require                     );
        GLSLC(0, #define DEBUG                                                );
    }

    if (stage == VK_SHADER_STAGE_TASK_BIT_EXT ||
        stage == VK_SHADER_STAGE_MESH_BIT_EXT)
        GLSLC(0, #extension GL_EXT_mesh_shader : require                      );

    for (int i = 0; i < nb_extensions; i++)
        GLSLF(0, #extension %s : %s                  ,extensions[i], "require");
    GLSLC(0,                                                                  );

    GLSLF(0, layout (local_size_x = %i, local_size_y = %i, local_size_z = %i) in;
          , shd->lg_size[0], shd->lg_size[1], shd->lg_size[2]);
    GLSLC(0,                                                                  );

    return 0;
}

void ff_vk_shader_print(void *ctx, FFVulkanShader *shd, int prio)
{
    int line = 0;
    const char *p = shd->src.str;
    const char *start = p;
    const size_t len = strlen(p);

    AVBPrint buf;
    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    for (int i = 0; i < len; i++) {
        if (p[i] == '\n') {
            av_bprintf(&buf, "%i\t", ++line);
            av_bprint_append_data(&buf, start, &p[i] - start + 1);
            start = &p[i + 1];
        }
    }

    av_log(ctx, prio, "Shader %s: \n%s", shd->name, buf.str);
    av_bprint_finalize(&buf, NULL);
}

static int init_pipeline_layout(FFVulkanContext *s, FFVulkanShader *shd)
{
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;
    VkPipelineLayoutCreateInfo pipeline_layout_info;

    /* Finally create the pipeline layout */
    pipeline_layout_info = (VkPipelineLayoutCreateInfo) {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pSetLayouts            = shd->desc_layout,
        .setLayoutCount         = shd->nb_descriptor_sets,
        .pushConstantRangeCount = shd->push_consts_num,
        .pPushConstantRanges    = shd->push_consts,
    };

    ret = vk->CreatePipelineLayout(s->hwctx->act_dev, &pipeline_layout_info,
                                   s->hwctx->alloc, &shd->pipeline_layout);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Unable to init pipeline layout: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int create_shader_module(FFVulkanContext *s, FFVulkanShader *shd,
                                VkShaderModule *mod,
                                uint8_t *spirv, size_t spirv_len)
{
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;

    VkShaderModuleCreateInfo shader_module_info = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext    = NULL,
        .flags    = 0x0,
        .pCode    = (void *)spirv,
        .codeSize = spirv_len,
    };

    ret = vk->CreateShaderModule(s->hwctx->act_dev, &shader_module_info,
                                 s->hwctx->alloc, mod);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Error creating shader module: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int init_compute_pipeline(FFVulkanContext *s, FFVulkanShader *shd,
                                 VkShaderModule mod, const char *entrypoint)
{
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;

    VkComputePipelineCreateInfo pipeline_create_info = {
        .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .flags = (s->extensions & FF_VK_EXT_DESCRIPTOR_BUFFER) ?
                 VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT : 0x0,
        .layout = shd->pipeline_layout,
        .stage = (VkPipelineShaderStageCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = shd->subgroup_info.requiredSubgroupSize ?
                     &shd->subgroup_info : NULL,
            .pName = entrypoint,
            .flags = shd->subgroup_info.requiredSubgroupSize ?
                     VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT : 0x0,
            .stage = shd->stage,
            .module = mod,
        },
    };

    ret = vk->CreateComputePipelines(s->hwctx->act_dev, VK_NULL_HANDLE, 1,
                                     &pipeline_create_info,
                                     s->hwctx->alloc, &shd->pipeline);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Unable to init compute pipeline: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int create_shader_object(FFVulkanContext *s, FFVulkanShader *shd,
                                uint8_t *spirv, size_t spirv_len,
                                const char *entrypoint)
{
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;
    size_t shader_size = 0;

    VkShaderCreateInfoEXT shader_obj_create = {
        .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
        .flags = shd->subgroup_info.requiredSubgroupSize ?
                 VK_SHADER_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT : 0x0,
        .stage = shd->stage,
        .nextStage = 0,
        .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
        .pCode = spirv,
        .codeSize = spirv_len,
        .pName = entrypoint,
        .pSetLayouts = shd->desc_layout,
        .setLayoutCount = shd->nb_descriptor_sets,
        .pushConstantRangeCount = shd->push_consts_num,
        .pPushConstantRanges = shd->push_consts,
        .pSpecializationInfo = NULL,
    };

    ret = vk->CreateShadersEXT(s->hwctx->act_dev, 1, &shader_obj_create,
                               s->hwctx->alloc, &shd->object);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Unable to create shader object: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    if (vk->GetShaderBinaryDataEXT(s->hwctx->act_dev, shd->object,
                                   &shader_size, NULL) == VK_SUCCESS)
        av_log(s, AV_LOG_VERBOSE, "Shader %s size: %zu binary (%zu SPIR-V)\n",
               shd->name, shader_size, spirv_len);

    return 0;
}

static int init_descriptors(FFVulkanContext *s, FFVulkanShader *shd)
{
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;

    shd->desc_layout = av_malloc_array(shd->nb_descriptor_sets,
                                       sizeof(*shd->desc_layout));
    if (!shd->desc_layout)
        return AVERROR(ENOMEM);

    if (!(s->extensions & FF_VK_EXT_DESCRIPTOR_BUFFER)) {
        int has_singular = 0;
        int max_descriptors = 0;
        for (int i = 0; i < shd->nb_descriptor_sets; i++) {
            max_descriptors = FFMAX(max_descriptors, shd->desc_set[i].nb_bindings);
            if (shd->desc_set[i].singular)
                has_singular = 1;
        }
        shd->use_push = (s->extensions & FF_VK_EXT_PUSH_DESCRIPTOR) &&
                        (max_descriptors <= s->push_desc_props.maxPushDescriptors) &&
                        (shd->nb_descriptor_sets == 1) &&
                        (has_singular == 0);
    }

    for (int i = 0; i < shd->nb_descriptor_sets; i++) {
        FFVulkanDescriptorSet *set = &shd->desc_set[i];
        VkDescriptorSetLayoutCreateInfo desc_layout_create = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = set->nb_bindings,
            .pBindings = set->binding,
            .flags = (s->extensions & FF_VK_EXT_DESCRIPTOR_BUFFER) ?
                     VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT :
                     (shd->use_push) ?
                     VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR :
                     0x0,
        };

        ret = vk->CreateDescriptorSetLayout(s->hwctx->act_dev,
                                            &desc_layout_create,
                                            s->hwctx->alloc,
                                            &shd->desc_layout[i]);
        if (ret != VK_SUCCESS) {
            av_log(s, AV_LOG_ERROR, "Unable to create descriptor set layout: %s",
                   ff_vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }

        if (s->extensions & FF_VK_EXT_DESCRIPTOR_BUFFER) {
            vk->GetDescriptorSetLayoutSizeEXT(s->hwctx->act_dev, shd->desc_layout[i],
                                              &set->layout_size);

            set->aligned_size = FFALIGN(set->layout_size,
                                        s->desc_buf_props.descriptorBufferOffsetAlignment);

            for (int j = 0; j < set->nb_bindings; j++)
                vk->GetDescriptorSetLayoutBindingOffsetEXT(s->hwctx->act_dev,
                                                           shd->desc_layout[i],
                                                           j,
                                                           &set->binding_offset[j]);
        }
    }

    return 0;
}

int ff_vk_shader_link(FFVulkanContext *s, FFVulkanShader *shd,
                      uint8_t *spirv, size_t spirv_len,
                      const char *entrypoint)
{
    int err;
    FFVulkanFunctions *vk = &s->vkfn;

    err = init_descriptors(s, shd);
    if (err < 0)
        return err;

    err = init_pipeline_layout(s, shd);
    if (err < 0)
        return err;

    if (s->extensions & FF_VK_EXT_DESCRIPTOR_BUFFER) {
        shd->bound_buffer_indices = av_calloc(shd->nb_descriptor_sets,
                                              sizeof(*shd->bound_buffer_indices));
        if (!shd->bound_buffer_indices)
            return AVERROR(ENOMEM);

        for (int i = 0; i < shd->nb_descriptor_sets; i++)
            shd->bound_buffer_indices[i] = i;
    }

    if (s->extensions & FF_VK_EXT_SHADER_OBJECT) {
        err = create_shader_object(s, shd, spirv, spirv_len, entrypoint);
        if (err < 0)
            return err;
    } else {
        VkShaderModule mod;
        err = create_shader_module(s, shd, &mod, spirv, spirv_len);
        if (err < 0)
            return err;

        switch (shd->bind_point) {
        case VK_PIPELINE_BIND_POINT_COMPUTE:
            err = init_compute_pipeline(s, shd, mod, entrypoint);
            break;
        default:
            av_log(s, AV_LOG_ERROR, "Unsupported shader type: %i\n",
                   shd->bind_point);
            err = AVERROR(EINVAL);
            break;
        };

        vk->DestroyShaderModule(s->hwctx->act_dev, mod, s->hwctx->alloc);
        if (err < 0)
            return err;
    }

    return 0;
}

static const struct descriptor_props {
    size_t struct_size; /* Size of the opaque which updates the descriptor */
    const char *type;
    int is_uniform;
    int mem_quali;      /* Can use a memory qualifier */
    int dim_needed;     /* Must indicate dimension */
    int buf_content;    /* Must indicate buffer contents */
} descriptor_props[] = {
    [VK_DESCRIPTOR_TYPE_SAMPLER]                = { sizeof(VkDescriptorImageInfo),  "sampler",       1, 0, 0, 0, },
    [VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE]          = { sizeof(VkDescriptorImageInfo),  "texture",       1, 0, 1, 0, },
    [VK_DESCRIPTOR_TYPE_STORAGE_IMAGE]          = { sizeof(VkDescriptorImageInfo),  "image",         1, 1, 1, 0, },
    [VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT]       = { sizeof(VkDescriptorImageInfo),  "subpassInput",  1, 0, 0, 0, },
    [VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER] = { sizeof(VkDescriptorImageInfo),  "sampler",       1, 0, 1, 0, },
    [VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER]         = { sizeof(VkDescriptorBufferInfo),  NULL,           1, 0, 0, 1, },
    [VK_DESCRIPTOR_TYPE_STORAGE_BUFFER]         = { sizeof(VkDescriptorBufferInfo), "buffer",        0, 1, 0, 1, },
    [VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC] = { sizeof(VkDescriptorBufferInfo),  NULL,           1, 0, 0, 1, },
    [VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC] = { sizeof(VkDescriptorBufferInfo), "buffer",        0, 1, 0, 1, },
    [VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER]   = { sizeof(VkBufferView),           "samplerBuffer", 1, 0, 0, 0, },
    [VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER]   = { sizeof(VkBufferView),           "imageBuffer",   1, 0, 0, 0, },
};

int ff_vk_shader_add_descriptor_set(FFVulkanContext *s, FFVulkanShader *shd,
                                    FFVulkanDescriptorSetBinding *desc, int nb,
                                    int singular, int print_to_shader_only)
{
    int has_sampler = 0;
    FFVulkanDescriptorSet *set;

    if (print_to_shader_only)
        goto print;

    /* Actual layout allocated for the pipeline */
    set = av_realloc_array(shd->desc_set,
                           sizeof(*shd->desc_set),
                           shd->nb_descriptor_sets + 1);
    if (!set)
        return AVERROR(ENOMEM);
    shd->desc_set = set;

    set = &set[shd->nb_descriptor_sets];
    memset(set, 0, sizeof(*set));

    set->binding = av_calloc(nb, sizeof(*set->binding));
    if (!set->binding)
        return AVERROR(ENOMEM);

    set->binding_offset = av_calloc(nb, sizeof(*set->binding_offset));
    if (!set->binding_offset) {
        av_freep(&set->binding);
        return AVERROR(ENOMEM);
    }

    for (int i = 0; i < nb; i++) {
        set->binding[i].binding            = i;
        set->binding[i].descriptorType     = desc[i].type;
        set->binding[i].descriptorCount    = FFMAX(desc[i].elems, 1);
        set->binding[i].stageFlags         = desc[i].stages;
        set->binding[i].pImmutableSamplers = desc[i].samplers;

        if (desc[i].type == VK_DESCRIPTOR_TYPE_SAMPLER ||
            desc[i].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            has_sampler |= 1;
    }

    set->usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (has_sampler)
        set->usage |= VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;

    if (!(s->extensions & FF_VK_EXT_DESCRIPTOR_BUFFER)) {
        for (int i = 0; i < nb; i++) {
            int j;
            VkDescriptorPoolSize *desc_pool_size;
            for (j = 0; j < shd->nb_desc_pool_size; j++)
                if (shd->desc_pool_size[j].type == desc[i].type)
                    break;
            if (j >= shd->nb_desc_pool_size) {
                desc_pool_size = av_realloc_array(shd->desc_pool_size,
                                                  sizeof(*desc_pool_size),
                                                  shd->nb_desc_pool_size + 1);
                if (!desc_pool_size)
                    return AVERROR(ENOMEM);

                shd->desc_pool_size = desc_pool_size;
                shd->nb_desc_pool_size++;
                memset(&desc_pool_size[j], 0, sizeof(VkDescriptorPoolSize));
            }
            shd->desc_pool_size[j].type             = desc[i].type;
            shd->desc_pool_size[j].descriptorCount += FFMAX(desc[i].elems, 1);
        }
    }

    set->singular = singular;
    set->nb_bindings = nb;
    shd->nb_descriptor_sets++;

print:
    /* Write shader info */
    for (int i = 0; i < nb; i++) {
        const struct descriptor_props *prop = &descriptor_props[desc[i].type];
        GLSLA("layout (set = %i, binding = %i", FFMAX(shd->nb_descriptor_sets - 1, 0), i);

        if (desc[i].mem_layout &&
            (desc[i].type != VK_DESCRIPTOR_TYPE_STORAGE_IMAGE))
            GLSLA(", %s", desc[i].mem_layout);

        GLSLA(")");

        if (prop->is_uniform)
            GLSLA(" uniform");

        if (prop->mem_quali && desc[i].mem_quali)
            GLSLA(" %s", desc[i].mem_quali);

        if (prop->type) {
            GLSLA(" ");
            if (desc[i].type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
                if (desc[i].mem_layout) {
                    int len = strlen(desc[i].mem_layout);
                    if (desc[i].mem_layout[len - 1] == 'i' &&
                        desc[i].mem_layout[len - 2] == 'u') {
                        GLSLA("u");
                    } else if (desc[i].mem_layout[len - 1] == 'i') {
                        GLSLA("i");
                    }
                }
            }
            GLSLA("%s", prop->type);
        }

        if (prop->dim_needed)
            GLSLA("%iD", desc[i].dimensions);

        GLSLA(" %s", desc[i].name);

        if (prop->buf_content) {
            GLSLA(" {\n    ");
            if (desc[i].buf_elems) {
                GLSLA("%s", desc[i].buf_content);
                GLSLA("[%i];", desc[i].buf_elems);
            } else {
                GLSLA("%s", desc[i].buf_content);
            }
            GLSLA("\n}");
        } else if (desc[i].elems > 0) {
            GLSLA("[%i]", desc[i].elems);
        }

        GLSLA(";");
        GLSLA("\n");
    }
    GLSLA("\n");

    return 0;
}

int ff_vk_shader_register_exec(FFVulkanContext *s, FFVkExecPool *pool,
                               FFVulkanShader *shd)
{
    int err;
    FFVulkanShaderData *sd;

    if (!shd->nb_descriptor_sets)
        return 0;

    sd = av_realloc_array(pool->reg_shd,
                          sizeof(*pool->reg_shd),
                          pool->nb_reg_shd + 1);
    if (!sd)
        return AVERROR(ENOMEM);

    pool->reg_shd = sd;
    sd = &sd[pool->nb_reg_shd++];
    memset(sd, 0, sizeof(*sd));

    sd->shd = shd;
    sd->nb_descriptor_sets = shd->nb_descriptor_sets;

    if (s->extensions & FF_VK_EXT_DESCRIPTOR_BUFFER) {
        sd->desc_bind = av_malloc_array(sd->nb_descriptor_sets, sizeof(*sd->desc_bind));
        if (!sd->desc_bind)
            return AVERROR(ENOMEM);

        sd->desc_set_buf = av_calloc(sd->nb_descriptor_sets, sizeof(*sd->desc_set_buf));
        if (!sd->desc_set_buf)
            return AVERROR(ENOMEM);

        for (int i = 0; i < sd->nb_descriptor_sets; i++) {
            FFVulkanDescriptorSet *set = &shd->desc_set[i];
            FFVulkanDescriptorSetData *sdb = &sd->desc_set_buf[i];
            int nb = set->singular ? 1 : pool->pool_size;

            err = ff_vk_create_buf(s, &sdb->buf,
                                   set->aligned_size*nb,
                                   NULL, NULL, set->usage,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (err < 0)
                return err;

            err = ff_vk_map_buffer(s, &sdb->buf, &sdb->desc_mem, 0);
            if (err < 0)
                return err;

            sd->desc_bind[i] = (VkDescriptorBufferBindingInfoEXT) {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT,
                .usage = set->usage,
                .address = sdb->buf.address,
            };
        }
    } else if (!shd->use_push) {
        VkResult ret;
        FFVulkanFunctions *vk = &s->vkfn;
        VkDescriptorSetLayout *tmp_layouts;
        VkDescriptorSetAllocateInfo set_alloc_info;
        VkDescriptorPoolCreateInfo pool_create_info;

        for (int i = 0; i < shd->nb_desc_pool_size; i++)
            shd->desc_pool_size[i].descriptorCount *= pool->pool_size;

        pool_create_info = (VkDescriptorPoolCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = 0,
            .pPoolSizes = shd->desc_pool_size,
            .poolSizeCount = shd->nb_desc_pool_size,
            .maxSets = sd->nb_descriptor_sets*pool->pool_size,
        };

        ret = vk->CreateDescriptorPool(s->hwctx->act_dev, &pool_create_info,
                                       s->hwctx->alloc, &sd->desc_pool);
        if (ret != VK_SUCCESS) {
            av_log(s, AV_LOG_ERROR, "Unable to create descriptor pool: %s\n",
                   ff_vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }

        tmp_layouts = av_malloc_array(pool_create_info.maxSets, sizeof(*tmp_layouts));
        if (!tmp_layouts)
            return AVERROR(ENOMEM);

        /* Colate each execution context's descriptor set layouts */
        for (int i = 0; i < pool->pool_size; i++)
            for (int j = 0; j < sd->nb_descriptor_sets; j++)
                tmp_layouts[i*sd->nb_descriptor_sets + j] = shd->desc_layout[j];

        set_alloc_info = (VkDescriptorSetAllocateInfo) {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = sd->desc_pool,
            .pSetLayouts        = tmp_layouts,
            .descriptorSetCount = pool_create_info.maxSets,
        };

        sd->desc_sets = av_malloc_array(pool_create_info.maxSets,
                                        sizeof(*tmp_layouts));
        if (!sd->desc_sets) {
            av_free(tmp_layouts);
            return AVERROR(ENOMEM);
        }
        ret = vk->AllocateDescriptorSets(s->hwctx->act_dev, &set_alloc_info,
                                         sd->desc_sets);
        av_free(tmp_layouts);
        if (ret != VK_SUCCESS) {
            av_log(s, AV_LOG_ERROR, "Unable to allocate descriptor set: %s\n",
                   ff_vk_ret2str(ret));
            av_freep(&sd->desc_sets);
            return AVERROR_EXTERNAL;
        }
    }

    return 0;
}

static inline FFVulkanShaderData *get_shd_data(FFVkExecContext *e,
                                               FFVulkanShader *shd)
{
    for (int i = 0; i < e->parent->nb_reg_shd; i++)
        if (e->parent->reg_shd[i].shd == shd)
            return &e->parent->reg_shd[i];
    return NULL;
}

static inline void update_set_descriptor(FFVulkanContext *s, FFVkExecContext *e,
                                         FFVulkanShader *shd, int set,
                                         int bind_idx, int array_idx,
                                         VkDescriptorGetInfoEXT *desc_get_info,
                                         size_t desc_size)
{
    FFVulkanFunctions *vk = &s->vkfn;
    FFVulkanDescriptorSet *desc_set = &shd->desc_set[set];
    FFVulkanShaderData *sd = get_shd_data(e, shd);
    const size_t exec_offset = desc_set->singular ? 0 : desc_set->aligned_size*e->idx;

    void *desc = sd->desc_set_buf[set].desc_mem +     /* Base */
                 exec_offset +                        /* Execution context */
                 desc_set->binding_offset[bind_idx] + /* Descriptor binding */
                 array_idx*desc_size;                 /* Array position */

    vk->GetDescriptorEXT(s->hwctx->act_dev, desc_get_info, desc_size, desc);
}

static inline void update_set_pool_write(FFVulkanContext *s, FFVkExecContext *e,
                                         FFVulkanShader *shd, int set,
                                         VkWriteDescriptorSet *write_info)
{
    FFVulkanFunctions *vk = &s->vkfn;
    FFVulkanDescriptorSet *desc_set = &shd->desc_set[set];
    FFVulkanShaderData *sd = get_shd_data(e, shd);

    if (desc_set->singular) {
        for (int i = 0; i < e->parent->pool_size; i++) {
            write_info->dstSet = sd->desc_sets[i*sd->nb_descriptor_sets + set];
            vk->UpdateDescriptorSets(s->hwctx->act_dev, 1, write_info, 0, NULL);
        }
    } else {
        if (shd->use_push) {
            vk->CmdPushDescriptorSetKHR(e->buf,
                                        shd->bind_point,
                                        shd->pipeline_layout,
                                        set, 1,
                                        write_info);
        } else {
            write_info->dstSet = sd->desc_sets[e->idx*sd->nb_descriptor_sets + set];
            vk->UpdateDescriptorSets(s->hwctx->act_dev, 1, write_info, 0, NULL);
        }
    }
}

int ff_vk_shader_update_img(FFVulkanContext *s, FFVkExecContext *e,
                            FFVulkanShader *shd, int set, int bind, int offs,
                            VkImageView view, VkImageLayout layout,
                            VkSampler sampler)
{
    FFVulkanDescriptorSet *desc_set = &shd->desc_set[set];

    if (s->extensions & FF_VK_EXT_DESCRIPTOR_BUFFER) {
        VkDescriptorGetInfoEXT desc_get_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
            .type = desc_set->binding[bind].descriptorType,
        };
        VkDescriptorImageInfo desc_img_info = {
            .imageView = view,
            .sampler = sampler,
            .imageLayout = layout,
        };
        size_t desc_size;

        switch (desc_get_info.type) {
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            desc_get_info.data.pSampledImage = &desc_img_info;
            desc_size = s->desc_buf_props.sampledImageDescriptorSize;
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            desc_get_info.data.pStorageImage = &desc_img_info;
            desc_size = s->desc_buf_props.storageImageDescriptorSize;
            break;
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            desc_get_info.data.pInputAttachmentImage = &desc_img_info;
            desc_size = s->desc_buf_props.inputAttachmentDescriptorSize;
            break;
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            desc_get_info.data.pCombinedImageSampler = &desc_img_info;
            desc_size = s->desc_buf_props.combinedImageSamplerDescriptorSize;
            break;
        default:
            av_log(s, AV_LOG_ERROR, "Invalid descriptor type at set %i binding %i: %i!\n",
                   set, bind, desc_get_info.type);
            return AVERROR(EINVAL);
            break;
        };

        update_set_descriptor(s, e, shd, set, bind, offs,
                              &desc_get_info, desc_size);
    } else {
        VkDescriptorImageInfo desc_pool_write_info_img = {
            .sampler = sampler,
            .imageView = view,
            .imageLayout = layout,
        };
        VkWriteDescriptorSet desc_pool_write_info = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = bind,
            .descriptorCount = 1,
            .dstArrayElement = offs,
            .descriptorType = desc_set->binding[bind].descriptorType,
            .pImageInfo = &desc_pool_write_info_img,
        };
        update_set_pool_write(s, e, shd, set, &desc_pool_write_info);
    }

    return 0;
}

void ff_vk_shader_update_img_array(FFVulkanContext *s, FFVkExecContext *e,
                                   FFVulkanShader *shd, AVFrame *f,
                                   VkImageView *views, int set, int binding,
                                   VkImageLayout layout, VkSampler sampler)
{
    AVHWFramesContext *hwfc = (AVHWFramesContext *)f->hw_frames_ctx->data;
    const int nb_planes = av_pix_fmt_count_planes(hwfc->sw_format);

    for (int i = 0; i < nb_planes; i++)
        ff_vk_shader_update_img(s, e, shd, set, binding, i,
                                views[i], layout, sampler);
}

int ff_vk_shader_update_desc_buffer(FFVulkanContext *s, FFVkExecContext *e,
                                    FFVulkanShader *shd,
                                    int set, int bind, int elem,
                                    FFVkBuffer *buf, VkDeviceSize offset, VkDeviceSize len,
                                    VkFormat fmt)
{
    FFVulkanDescriptorSet *desc_set = &shd->desc_set[set];

    if (s->extensions & FF_VK_EXT_DESCRIPTOR_BUFFER) {
        VkDescriptorGetInfoEXT desc_get_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
            .type = desc_set->binding[bind].descriptorType,
        };
        VkDescriptorAddressInfoEXT desc_buf_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT,
            .address = buf->address + offset,
            .range = len,
            .format = fmt,
        };
        size_t desc_size;

        switch (desc_get_info.type) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            desc_get_info.data.pUniformBuffer = &desc_buf_info;
            desc_size = s->desc_buf_props.uniformBufferDescriptorSize;
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            desc_get_info.data.pStorageBuffer = &desc_buf_info;
            desc_size = s->desc_buf_props.storageBufferDescriptorSize;
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            desc_get_info.data.pUniformTexelBuffer = &desc_buf_info;
            desc_size = s->desc_buf_props.uniformTexelBufferDescriptorSize;
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            desc_get_info.data.pStorageTexelBuffer = &desc_buf_info;
            desc_size = s->desc_buf_props.storageTexelBufferDescriptorSize;
            break;
        default:
            av_log(s, AV_LOG_ERROR, "Invalid descriptor type at set %i binding %i: %i!\n",
                   set, bind, desc_get_info.type);
            return AVERROR(EINVAL);
            break;
        };

        update_set_descriptor(s, e, shd, set, bind, elem, &desc_get_info, desc_size);
    } else {
        VkDescriptorBufferInfo desc_pool_write_info_buf = {
            .buffer = buf->buf,
            .offset = buf->virtual_offset + offset,
            .range = len,
        };
        VkWriteDescriptorSet desc_pool_write_info = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = bind,
            .descriptorCount = 1,
            .dstArrayElement = elem,
            .descriptorType = desc_set->binding[bind].descriptorType,
            .pBufferInfo = &desc_pool_write_info_buf,
        };
        update_set_pool_write(s, e, shd, set, &desc_pool_write_info);
    }

    return 0;
}

void ff_vk_shader_update_push_const(FFVulkanContext *s, FFVkExecContext *e,
                                    FFVulkanShader *shd,
                                    VkShaderStageFlagBits stage,
                                    int offset, size_t size, void *src)
{
    FFVulkanFunctions *vk = &s->vkfn;
    vk->CmdPushConstants(e->buf, shd->pipeline_layout,
                         stage, offset, size, src);
}

void ff_vk_exec_bind_shader(FFVulkanContext *s, FFVkExecContext *e,
                            FFVulkanShader *shd)
{
    FFVulkanFunctions *vk = &s->vkfn;
    VkDeviceSize offsets[1024];
    FFVulkanShaderData *sd = get_shd_data(e, shd);

    if (s->extensions & FF_VK_EXT_SHADER_OBJECT) {
        VkShaderStageFlagBits stages = shd->stage;
        vk->CmdBindShadersEXT(e->buf, 1, &stages, &shd->object);
    } else {
        vk->CmdBindPipeline(e->buf, shd->bind_point, shd->pipeline);
    }

    if (sd && sd->nb_descriptor_sets) {
        if (s->extensions & FF_VK_EXT_DESCRIPTOR_BUFFER) {
            for (int i = 0; i < sd->nb_descriptor_sets; i++)
                offsets[i] = shd->desc_set[i].singular ? 0 : shd->desc_set[i].aligned_size*e->idx;

            /* Bind descriptor buffers */
            vk->CmdBindDescriptorBuffersEXT(e->buf, sd->nb_descriptor_sets, sd->desc_bind);
            /* Binding offsets */
            vk->CmdSetDescriptorBufferOffsetsEXT(e->buf, shd->bind_point, shd->pipeline_layout,
                                                 0, sd->nb_descriptor_sets,
                                                 shd->bound_buffer_indices, offsets);
        } else if (!shd->use_push) {
            vk->CmdBindDescriptorSets(e->buf, shd->bind_point, shd->pipeline_layout,
                                      0, sd->nb_descriptor_sets,
                                      &sd->desc_sets[e->idx*sd->nb_descriptor_sets],
                                      0, NULL);
        }
    }
}

void ff_vk_shader_free(FFVulkanContext *s, FFVulkanShader *shd)
{
    FFVulkanFunctions *vk = &s->vkfn;

    av_bprint_finalize(&shd->src, NULL);

#if 0
    if (shd->shader.module)
        vk->DestroyShaderModule(s->hwctx->act_dev, shd->shader.module,
                                s->hwctx->alloc);
#endif

    if (shd->object)
        vk->DestroyShaderEXT(s->hwctx->act_dev, shd->object, s->hwctx->alloc);
    if (shd->pipeline)
        vk->DestroyPipeline(s->hwctx->act_dev, shd->pipeline, s->hwctx->alloc);
    if (shd->pipeline_layout)
        vk->DestroyPipelineLayout(s->hwctx->act_dev, shd->pipeline_layout,
                                  s->hwctx->alloc);

    for (int i = 0; i < shd->nb_descriptor_sets; i++) {
        FFVulkanDescriptorSet *set = &shd->desc_set[i];
        av_free(set->binding);
        av_free(set->binding_offset);
    }

    if (shd->desc_layout) {
        for (int i = 0; i < shd->nb_descriptor_sets; i++)
            if (shd->desc_layout[i])
                vk->DestroyDescriptorSetLayout(s->hwctx->act_dev, shd->desc_layout[i],
                                               s->hwctx->alloc);
    }

    av_freep(&shd->desc_pool_size);
    av_freep(&shd->desc_layout);
    av_freep(&shd->desc_set);
    av_freep(&shd->bound_buffer_indices);
    av_freep(&shd->push_consts);
    shd->push_consts_num = 0;
}

void ff_vk_uninit(FFVulkanContext *s)
{
    av_freep(&s->query_props);
    av_freep(&s->qf_props);
    av_freep(&s->video_props);
    av_freep(&s->coop_mat_props);
    av_freep(&s->host_image_copy_layouts);

    av_buffer_unref(&s->device_ref);
    av_buffer_unref(&s->frames_ref);
}

int ff_vk_init(FFVulkanContext *s, void *log_parent,
               AVBufferRef *device_ref, AVBufferRef *frames_ref)
{
    int err;

    static const AVClass vulkan_context_class = {
        .class_name       = "vk",
        .version          = LIBAVUTIL_VERSION_INT,
        .parent_log_context_offset = offsetof(FFVulkanContext, log_parent),
    };

    memset(s, 0, sizeof(*s));
    s->log_parent = log_parent;
    s->class      = &vulkan_context_class;

    if (frames_ref) {
        s->frames_ref = av_buffer_ref(frames_ref);
        if (!s->frames_ref)
            return AVERROR(ENOMEM);

        s->frames = (AVHWFramesContext *)s->frames_ref->data;
        s->hwfc = s->frames->hwctx;

        device_ref = s->frames->device_ref;
    }

    s->device_ref = av_buffer_ref(device_ref);
    if (!s->device_ref) {
        ff_vk_uninit(s);
        return AVERROR(ENOMEM);
    }

    s->device = (AVHWDeviceContext *)s->device_ref->data;
    s->hwctx = s->device->hwctx;

    s->extensions = ff_vk_extensions_to_mask(s->hwctx->enabled_dev_extensions,
                                             s->hwctx->nb_enabled_dev_extensions);
    s->extensions |= ff_vk_extensions_to_mask(s->hwctx->enabled_inst_extensions,
                                              s->hwctx->nb_enabled_inst_extensions);

    err = ff_vk_load_functions(s->device, &s->vkfn, s->extensions, 1, 1);
    if (err < 0) {
        ff_vk_uninit(s);
        return err;
    }

    err = ff_vk_load_props(s);
    if (err < 0) {
        ff_vk_uninit(s);
        return err;
    }

    return 0;
}
