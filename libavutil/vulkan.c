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

#include "avassert.h"

#include "vulkan.h"
#include "vulkan_loader.h"

#if CONFIG_LIBGLSLANG
#include "vulkan_glslang.c"
#elif CONFIG_LIBSHADERC
#include "vulkan_shaderc.c"
#endif

/* Generic macro for creating contexts which need to keep their addresses
 * if another context is created. */
#define FN_CREATING(ctx, type, shortname, array, num)                          \
static av_always_inline type *create_ ##shortname(ctx *dctx)                   \
{                                                                              \
    type **array, *sctx = av_mallocz(sizeof(*sctx));                           \
    if (!sctx)                                                                 \
        return NULL;                                                           \
                                                                               \
    array = av_realloc_array(dctx->array, sizeof(*dctx->array), dctx->num + 1);\
    if (!array) {                                                              \
        av_free(sctx);                                                         \
        return NULL;                                                           \
    }                                                                          \
                                                                               \
    dctx->array = array;                                                       \
    dctx->array[dctx->num++] = sctx;                                           \
                                                                               \
    return sctx;                                                               \
}

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
    CASE(VK_ERROR_SURFACE_LOST_KHR);
    CASE(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
    CASE(VK_SUBOPTIMAL_KHR);
    CASE(VK_ERROR_OUT_OF_DATE_KHR);
    CASE(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR);
    CASE(VK_ERROR_VALIDATION_FAILED_EXT);
    CASE(VK_ERROR_INVALID_SHADER_NV);
    CASE(VK_ERROR_OUT_OF_POOL_MEMORY);
    CASE(VK_ERROR_INVALID_EXTERNAL_HANDLE);
    CASE(VK_ERROR_NOT_PERMITTED_EXT);
    default: return "Unknown error";
    }
#undef CASE
}

void ff_vk_qf_init(FFVulkanContext *s, FFVkQueueFamilyCtx *qf,
                   VkQueueFlagBits dev_family, int nb_queues)
{
    switch (dev_family) {
    case VK_QUEUE_GRAPHICS_BIT:
        qf->queue_family = s->hwctx->queue_family_index;
        qf->actual_queues = s->hwctx->nb_graphics_queues;
        break;
    case VK_QUEUE_COMPUTE_BIT:
        qf->queue_family = s->hwctx->queue_family_comp_index;
        qf->actual_queues = s->hwctx->nb_comp_queues;
        break;
    case VK_QUEUE_TRANSFER_BIT:
        qf->queue_family = s->hwctx->queue_family_tx_index;
        qf->actual_queues = s->hwctx->nb_tx_queues;
        break;
    case VK_QUEUE_VIDEO_ENCODE_BIT_KHR:
        qf->queue_family = s->hwctx->queue_family_encode_index;
        qf->actual_queues = s->hwctx->nb_encode_queues;
        break;
    case VK_QUEUE_VIDEO_DECODE_BIT_KHR:
        qf->queue_family = s->hwctx->queue_family_decode_index;
        qf->actual_queues = s->hwctx->nb_decode_queues;
        break;
    default:
        av_assert0(0); /* Should never happen */
    }

    if (!nb_queues)
        qf->nb_queues = qf->actual_queues;
    else
        qf->nb_queues = nb_queues;

    return;
}

void ff_vk_qf_rotate(FFVkQueueFamilyCtx *qf)
{
    qf->cur_queue = (qf->cur_queue + 1) % qf->nb_queues;
}

static int vk_alloc_mem(FFVulkanContext *s, VkMemoryRequirements *req,
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

    /* Align if we need to */
    if (req_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        req->size = FFALIGN(req->size, s->props.limits.minMemoryMapAlignment);

    alloc_info.allocationSize = req->size;

    /* The vulkan spec requires memory types to be sorted in the "optimal"
     * order, so the first matching type we find will be the best/fastest one */
    for (int i = 0; i < s->mprops.memoryTypeCount; i++) {
        /* The memory type must be supported by the requirements (bitfield) */
        if (!(req->memoryTypeBits & (1 << i)))
            continue;

        /* The memory type flags must include our properties */
        if ((s->mprops.memoryTypes[i].propertyFlags & req_flags) != req_flags)
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
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Failed to allocate memory: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR(ENOMEM);
    }

    *mem_flags |= s->mprops.memoryTypes[index].propertyFlags;

    return 0;
}

int ff_vk_create_buf(FFVulkanContext *s, FFVkBuffer *buf, size_t size,
                     VkBufferUsageFlags usage, VkMemoryPropertyFlagBits flags)
{
    int err;
    VkResult ret;
    int use_ded_mem;
    FFVulkanFunctions *vk = &s->vkfn;

    VkBufferCreateInfo buf_spawn = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext       = NULL,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .size        = size, /* Gets FFALIGNED during alloc if host visible
                                but should be ok */
    };

    VkBufferMemoryRequirementsInfo2 req_desc = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
    };
    VkMemoryDedicatedAllocateInfo ded_alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .pNext = NULL,
    };
    VkMemoryDedicatedRequirements ded_req = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
    };
    VkMemoryRequirements2 req = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        .pNext = &ded_req,
    };

    ret = vk->CreateBuffer(s->hwctx->act_dev, &buf_spawn, NULL, &buf->buf);
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
    if (use_ded_mem)
        ded_alloc.buffer = buf->buf;

    err = vk_alloc_mem(s, &req.memoryRequirements, flags,
                       use_ded_mem ? &ded_alloc : (void *)ded_alloc.pNext,
                       &buf->flags, &buf->mem);
    if (err)
        return err;

    ret = vk->BindBufferMemory(s->hwctx->act_dev, buf->buf, buf->mem, 0);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Failed to bind memory to buffer: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    return 0;
}

int ff_vk_map_buffers(FFVulkanContext *s, FFVkBuffer *buf, uint8_t *mem[],
                      int nb_buffers, int invalidate)
{
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;
    VkMappedMemoryRange *inval_list = NULL;
    int inval_count = 0;

    for (int i = 0; i < nb_buffers; i++) {
        ret = vk->MapMemory(s->hwctx->act_dev, buf[i].mem, 0,
                            VK_WHOLE_SIZE, 0, (void **)&mem[i]);
        if (ret != VK_SUCCESS) {
            av_log(s, AV_LOG_ERROR, "Failed to map buffer memory: %s\n",
                   ff_vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }
    }

    if (!invalidate)
        return 0;

    for (int i = 0; i < nb_buffers; i++) {
        const VkMappedMemoryRange ival_buf = {
            .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = buf[i].mem,
            .size   = VK_WHOLE_SIZE,
        };
        if (buf[i].flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
            continue;
        inval_list = av_fast_realloc(s->scratch, &s->scratch_size,
                                     (++inval_count)*sizeof(*inval_list));
        if (!inval_list)
            return AVERROR(ENOMEM);
        inval_list[inval_count - 1] = ival_buf;
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

int ff_vk_unmap_buffers(FFVulkanContext *s, FFVkBuffer *buf, int nb_buffers,
                        int flush)
{
    int err = 0;
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;
    VkMappedMemoryRange *flush_list = NULL;
    int flush_count = 0;

    if (flush) {
        for (int i = 0; i < nb_buffers; i++) {
            const VkMappedMemoryRange flush_buf = {
                .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = buf[i].mem,
                .size   = VK_WHOLE_SIZE,
            };
            if (buf[i].flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                continue;
            flush_list = av_fast_realloc(s->scratch, &s->scratch_size,
                                         (++flush_count)*sizeof(*flush_list));
            if (!flush_list)
                return AVERROR(ENOMEM);
            flush_list[flush_count - 1] = flush_buf;
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

    for (int i = 0; i < nb_buffers; i++)
        vk->UnmapMemory(s->hwctx->act_dev, buf[i].mem);

    return err;
}

void ff_vk_free_buf(FFVulkanContext *s, FFVkBuffer *buf)
{
    FFVulkanFunctions *vk = &s->vkfn;

    if (!buf || !s->hwctx)
        return;

    vk->DeviceWaitIdle(s->hwctx->act_dev);

    if (buf->buf != VK_NULL_HANDLE)
        vk->DestroyBuffer(s->hwctx->act_dev, buf->buf, s->hwctx->alloc);
    if (buf->mem != VK_NULL_HANDLE)
        vk->FreeMemory(s->hwctx->act_dev, buf->mem, s->hwctx->alloc);
}

int ff_vk_add_push_constant(FFVulkanPipeline *pl, int offset, int size,
                            VkShaderStageFlagBits stage)
{
    VkPushConstantRange *pc;

    pl->push_consts = av_realloc_array(pl->push_consts, sizeof(*pl->push_consts),
                                       pl->push_consts_num + 1);
    if (!pl->push_consts)
        return AVERROR(ENOMEM);

    pc = &pl->push_consts[pl->push_consts_num++];
    memset(pc, 0, sizeof(*pc));

    pc->stageFlags = stage;
    pc->offset = offset;
    pc->size = size;

    return 0;
}

FN_CREATING(FFVulkanContext, FFVkExecContext, exec_ctx, exec_ctx, exec_ctx_num)
int ff_vk_create_exec_ctx(FFVulkanContext *s, FFVkExecContext **ctx,
                          FFVkQueueFamilyCtx *qf)
{
    VkResult ret;
    FFVkExecContext *e;
    FFVulkanFunctions *vk = &s->vkfn;

    VkCommandPoolCreateInfo cqueue_create = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags              = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex   = qf->queue_family,
    };
    VkCommandBufferAllocateInfo cbuf_create = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = qf->nb_queues,
    };

    e = create_exec_ctx(s);
    if (!e)
        return AVERROR(ENOMEM);

    e->qf = qf;

    e->queues = av_mallocz(qf->nb_queues * sizeof(*e->queues));
    if (!e->queues)
        return AVERROR(ENOMEM);

    e->bufs = av_mallocz(qf->nb_queues * sizeof(*e->bufs));
    if (!e->bufs)
        return AVERROR(ENOMEM);

    /* Create command pool */
    ret = vk->CreateCommandPool(s->hwctx->act_dev, &cqueue_create,
                              s->hwctx->alloc, &e->pool);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Command pool creation failure: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    cbuf_create.commandPool = e->pool;

    /* Allocate command buffer */
    ret = vk->AllocateCommandBuffers(s->hwctx->act_dev, &cbuf_create, e->bufs);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Command buffer alloc failure: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    for (int i = 0; i < qf->nb_queues; i++) {
        FFVkQueueCtx *q = &e->queues[i];
        vk->GetDeviceQueue(s->hwctx->act_dev, qf->queue_family,
                           i % qf->actual_queues, &q->queue);
    }

    *ctx = e;

    return 0;
}

void ff_vk_discard_exec_deps(FFVkExecContext *e)
{
    FFVkQueueCtx *q = &e->queues[e->qf->cur_queue];

    for (int j = 0; j < q->nb_buf_deps; j++)
        av_buffer_unref(&q->buf_deps[j]);
    q->nb_buf_deps = 0;

    for (int j = 0; j < q->nb_frame_deps; j++)
        av_frame_free(&q->frame_deps[j]);
    q->nb_frame_deps = 0;

    e->sem_wait_cnt = 0;
    e->sem_sig_cnt = 0;
}

int ff_vk_start_exec_recording(FFVulkanContext *s, FFVkExecContext *e)
{
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;
    FFVkQueueCtx *q = &e->queues[e->qf->cur_queue];

    VkCommandBufferBeginInfo cmd_start = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    /* Create the fence and don't wait for it initially */
    if (!q->fence) {
        VkFenceCreateInfo fence_spawn = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        };
        ret = vk->CreateFence(s->hwctx->act_dev, &fence_spawn, s->hwctx->alloc,
                              &q->fence);
        if (ret != VK_SUCCESS) {
            av_log(s, AV_LOG_ERROR, "Failed to queue frame fence: %s\n",
                   ff_vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }
    } else {
        vk->WaitForFences(s->hwctx->act_dev, 1, &q->fence, VK_TRUE, UINT64_MAX);
        vk->ResetFences(s->hwctx->act_dev, 1, &q->fence);
    }

    /* Discard queue dependencies */
    ff_vk_discard_exec_deps(e);

    ret = vk->BeginCommandBuffer(e->bufs[e->qf->cur_queue], &cmd_start);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Failed to start command recoding: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    return 0;
}

VkCommandBuffer ff_vk_get_exec_buf(FFVkExecContext *e)
{
    return e->bufs[e->qf->cur_queue];
}

int ff_vk_add_exec_dep(FFVulkanContext *s, FFVkExecContext *e, AVFrame *frame,
                       VkPipelineStageFlagBits in_wait_dst_flag)
{
    AVFrame **dst;
    AVVkFrame *f = (AVVkFrame *)frame->data[0];
    FFVkQueueCtx *q = &e->queues[e->qf->cur_queue];
    AVHWFramesContext *fc = (AVHWFramesContext *)frame->hw_frames_ctx->data;
    int planes = av_pix_fmt_count_planes(fc->sw_format);

    for (int i = 0; i < planes; i++) {
        e->sem_wait = av_fast_realloc(e->sem_wait, &e->sem_wait_alloc,
                                      (e->sem_wait_cnt + 1)*sizeof(*e->sem_wait));
        if (!e->sem_wait) {
            ff_vk_discard_exec_deps(e);
            return AVERROR(ENOMEM);
        }

        e->sem_wait_dst = av_fast_realloc(e->sem_wait_dst, &e->sem_wait_dst_alloc,
                                          (e->sem_wait_cnt + 1)*sizeof(*e->sem_wait_dst));
        if (!e->sem_wait_dst) {
            ff_vk_discard_exec_deps(e);
            return AVERROR(ENOMEM);
        }

        e->sem_wait_val = av_fast_realloc(e->sem_wait_val, &e->sem_wait_val_alloc,
                                          (e->sem_wait_cnt + 1)*sizeof(*e->sem_wait_val));
        if (!e->sem_wait_val) {
            ff_vk_discard_exec_deps(e);
            return AVERROR(ENOMEM);
        }

        e->sem_sig = av_fast_realloc(e->sem_sig, &e->sem_sig_alloc,
                                     (e->sem_sig_cnt + 1)*sizeof(*e->sem_sig));
        if (!e->sem_sig) {
            ff_vk_discard_exec_deps(e);
            return AVERROR(ENOMEM);
        }

        e->sem_sig_val = av_fast_realloc(e->sem_sig_val, &e->sem_sig_val_alloc,
                                         (e->sem_sig_cnt + 1)*sizeof(*e->sem_sig_val));
        if (!e->sem_sig_val) {
            ff_vk_discard_exec_deps(e);
            return AVERROR(ENOMEM);
        }

        e->sem_sig_val_dst = av_fast_realloc(e->sem_sig_val_dst, &e->sem_sig_val_dst_alloc,
                                             (e->sem_sig_cnt + 1)*sizeof(*e->sem_sig_val_dst));
        if (!e->sem_sig_val_dst) {
            ff_vk_discard_exec_deps(e);
            return AVERROR(ENOMEM);
        }

        e->sem_wait[e->sem_wait_cnt] = f->sem[i];
        e->sem_wait_dst[e->sem_wait_cnt] = in_wait_dst_flag;
        e->sem_wait_val[e->sem_wait_cnt] = f->sem_value[i];
        e->sem_wait_cnt++;

        e->sem_sig[e->sem_sig_cnt] = f->sem[i];
        e->sem_sig_val[e->sem_sig_cnt] = f->sem_value[i] + 1;
        e->sem_sig_val_dst[e->sem_sig_cnt] = &f->sem_value[i];
        e->sem_sig_cnt++;
    }

    dst = av_fast_realloc(q->frame_deps, &q->frame_deps_alloc_size,
                          (q->nb_frame_deps + 1) * sizeof(*dst));
    if (!dst) {
        ff_vk_discard_exec_deps(e);
        return AVERROR(ENOMEM);
    }

    q->frame_deps = dst;
    q->frame_deps[q->nb_frame_deps] = av_frame_clone(frame);
    if (!q->frame_deps[q->nb_frame_deps]) {
        ff_vk_discard_exec_deps(e);
        return AVERROR(ENOMEM);
    }
    q->nb_frame_deps++;

    return 0;
}

int ff_vk_submit_exec_queue(FFVulkanContext *s, FFVkExecContext *e)
{
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;
    FFVkQueueCtx *q = &e->queues[e->qf->cur_queue];

    VkTimelineSemaphoreSubmitInfo s_timeline_sem_info = {
        .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .pWaitSemaphoreValues = e->sem_wait_val,
        .pSignalSemaphoreValues = e->sem_sig_val,
        .waitSemaphoreValueCount = e->sem_wait_cnt,
        .signalSemaphoreValueCount = e->sem_sig_cnt,
    };

    VkSubmitInfo s_info = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext                = &s_timeline_sem_info,

        .commandBufferCount   = 1,
        .pCommandBuffers      = &e->bufs[e->qf->cur_queue],

        .pWaitSemaphores      = e->sem_wait,
        .pWaitDstStageMask    = e->sem_wait_dst,
        .waitSemaphoreCount   = e->sem_wait_cnt,

        .pSignalSemaphores    = e->sem_sig,
        .signalSemaphoreCount = e->sem_sig_cnt,
    };

    ret = vk->EndCommandBuffer(e->bufs[e->qf->cur_queue]);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Unable to finish command buffer: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    ret = vk->QueueSubmit(q->queue, 1, &s_info, q->fence);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Unable to submit command buffer: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    for (int i = 0; i < e->sem_sig_cnt; i++)
        *e->sem_sig_val_dst[i] += 1;

    return 0;
}

int ff_vk_add_dep_exec_ctx(FFVulkanContext *s, FFVkExecContext *e,
                           AVBufferRef **deps, int nb_deps)
{
    AVBufferRef **dst;
    FFVkQueueCtx *q = &e->queues[e->qf->cur_queue];

    if (!deps || !nb_deps)
        return 0;

    dst = av_fast_realloc(q->buf_deps, &q->buf_deps_alloc_size,
                          (q->nb_buf_deps + nb_deps) * sizeof(*dst));
    if (!dst)
        goto err;

    q->buf_deps = dst;

    for (int i = 0; i < nb_deps; i++) {
        q->buf_deps[q->nb_buf_deps] = deps[i];
        if (!q->buf_deps[q->nb_buf_deps])
            goto err;
        q->nb_buf_deps++;
    }

    return 0;

err:
    ff_vk_discard_exec_deps(e);
    return AVERROR(ENOMEM);
}

FN_CREATING(FFVulkanContext, FFVkSampler, sampler, samplers, samplers_num)
FFVkSampler *ff_vk_init_sampler(FFVulkanContext *s,
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

    FFVkSampler *sctx = create_sampler(s);
    if (!sctx)
        return NULL;

    ret = vk->CreateSampler(s->hwctx->act_dev, &sampler_info,
                            s->hwctx->alloc, &sctx->sampler[0]);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Unable to init sampler: %s\n",
               ff_vk_ret2str(ret));
        return NULL;
    }

    for (int i = 1; i < 4; i++)
        sctx->sampler[i] = sctx->sampler[0];

    return sctx;
}

int ff_vk_mt_is_np_rgb(enum AVPixelFormat pix_fmt)
{
    if (pix_fmt == AV_PIX_FMT_ABGR   || pix_fmt == AV_PIX_FMT_BGRA   ||
        pix_fmt == AV_PIX_FMT_RGBA   || pix_fmt == AV_PIX_FMT_RGB24  ||
        pix_fmt == AV_PIX_FMT_BGR24  || pix_fmt == AV_PIX_FMT_RGB48  ||
        pix_fmt == AV_PIX_FMT_RGBA64 || pix_fmt == AV_PIX_FMT_RGB565 ||
        pix_fmt == AV_PIX_FMT_BGR565 || pix_fmt == AV_PIX_FMT_BGR0   ||
        pix_fmt == AV_PIX_FMT_0BGR   || pix_fmt == AV_PIX_FMT_RGB0)
        return 1;
    return 0;
}

const char *ff_vk_shader_rep_fmt(enum AVPixelFormat pixfmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pixfmt);
    const int high = desc->comp[0].depth > 8;
    return high ? "rgba16f" : "rgba8";
}

typedef struct ImageViewCtx {
    VkImageView view;
} ImageViewCtx;

static void destroy_imageview(void *opaque, uint8_t *data)
{
    FFVulkanContext *s = opaque;
    FFVulkanFunctions *vk = &s->vkfn;
    ImageViewCtx *iv = (ImageViewCtx *)data;

    vk->DestroyImageView(s->hwctx->act_dev, iv->view, s->hwctx->alloc);
    av_free(iv);
}

int ff_vk_create_imageview(FFVulkanContext *s, FFVkExecContext *e,
                           VkImageView *v, VkImage img, VkFormat fmt,
                           const VkComponentMapping map)
{
    int err;
    AVBufferRef *buf;
    FFVulkanFunctions *vk = &s->vkfn;

    VkImageViewCreateInfo imgview_spawn = {
        .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext      = NULL,
        .image      = img,
        .viewType   = VK_IMAGE_VIEW_TYPE_2D,
        .format     = fmt,
        .components = map,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };

    ImageViewCtx *iv = av_mallocz(sizeof(*iv));

    VkResult ret = vk->CreateImageView(s->hwctx->act_dev, &imgview_spawn,
                                       s->hwctx->alloc, &iv->view);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Failed to create imageview: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    buf = av_buffer_create((uint8_t *)iv, sizeof(*iv), destroy_imageview, s, 0);
    if (!buf) {
        destroy_imageview(s, (uint8_t *)iv);
        return AVERROR(ENOMEM);
    }

    /* Add to queue dependencies */
    err = ff_vk_add_dep_exec_ctx(s, e, &buf, 1);
    if (err) {
        av_buffer_unref(&buf);
        return err;
    }

    *v = iv->view;

    return 0;
}

FN_CREATING(FFVulkanPipeline, FFVkSPIRVShader, shader, shaders, shaders_num)
FFVkSPIRVShader *ff_vk_init_shader(FFVulkanPipeline *pl, const char *name,
                                   VkShaderStageFlags stage)
{
    FFVkSPIRVShader *shd = create_shader(pl);
    if (!shd)
        return NULL;

    av_bprint_init(&shd->src, 0, AV_BPRINT_SIZE_UNLIMITED);

    shd->shader.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shd->shader.stage = stage;

    shd->name = name;

    GLSLF(0, #version %i                                                  ,460);
    GLSLC(0, #define IS_WITHIN(v1, v2) ((v1.x < v2.x) && (v1.y < v2.y))       );
    GLSLC(0,                                                                  );

    return shd;
}

void ff_vk_set_compute_shader_sizes(FFVkSPIRVShader *shd, int local_size[3])
{
    shd->local_size[0] = local_size[0];
    shd->local_size[1] = local_size[1];
    shd->local_size[2] = local_size[2];

    av_bprintf(&shd->src, "layout (local_size_x = %i, "
               "local_size_y = %i, local_size_z = %i) in;\n\n",
               shd->local_size[0], shd->local_size[1], shd->local_size[2]);
}

void ff_vk_print_shader(void *ctx, FFVkSPIRVShader *shd, int prio)
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

int ff_vk_compile_shader(FFVulkanContext *s, FFVkSPIRVShader *shd,
                         const char *entrypoint)
{
    int err;
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;
    VkShaderModuleCreateInfo shader_create;
    uint8_t *spirv;
    size_t spirv_size;
    void *priv;

    shd->shader.pName = entrypoint;

    if (!s->spirv_compiler) {
#if CONFIG_LIBGLSLANG
        s->spirv_compiler = ff_vk_glslang_init();
#elif CONFIG_LIBSHADERC
        s->spirv_compiler = ff_vk_shaderc_init();
#else
        return AVERROR(ENOSYS);
#endif
        if (!s->spirv_compiler)
            return AVERROR(ENOMEM);
    }

    err = s->spirv_compiler->compile_shader(s->spirv_compiler, s, shd, &spirv,
                                            &spirv_size, entrypoint, &priv);
    if (err < 0)
        return err;

    av_log(s, AV_LOG_VERBOSE, "Shader %s compiled! Size: %zu bytes\n",
           shd->name, spirv_size);

    shader_create.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_create.pNext    = NULL;
    shader_create.codeSize = spirv_size;
    shader_create.flags    = 0;
    shader_create.pCode    = (void *)spirv;

    ret = vk->CreateShaderModule(s->hwctx->act_dev, &shader_create, NULL,
                                 &shd->shader.module);

    s->spirv_compiler->free_shader(s->spirv_compiler, &priv);

    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Unable to create shader module: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
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

int ff_vk_add_descriptor_set(FFVulkanContext *s, FFVulkanPipeline *pl,
                             FFVkSPIRVShader *shd, FFVulkanDescriptorSetBinding *desc,
                             int num, int only_print_to_shader)
{
    VkResult ret;
    VkDescriptorSetLayout *layout;
    FFVulkanFunctions *vk = &s->vkfn;

    if (only_print_to_shader)
        goto print;

    pl->desc_layout = av_realloc_array(pl->desc_layout, sizeof(*pl->desc_layout),
                                       pl->desc_layout_num + pl->qf->nb_queues);
    if (!pl->desc_layout)
        return AVERROR(ENOMEM);

    pl->desc_set_initialized = av_realloc_array(pl->desc_set_initialized,
                                                sizeof(*pl->desc_set_initialized),
                                                pl->descriptor_sets_num + 1);
    if (!pl->desc_set_initialized)
        return AVERROR(ENOMEM);

    pl->desc_set_initialized[pl->descriptor_sets_num] = 0;
    layout = &pl->desc_layout[pl->desc_layout_num];

    { /* Create descriptor set layout descriptions */
        VkDescriptorSetLayoutCreateInfo desc_create_layout = { 0 };
        VkDescriptorSetLayoutBinding *desc_binding;

        desc_binding = av_mallocz(sizeof(*desc_binding)*num);
        if (!desc_binding)
            return AVERROR(ENOMEM);

        for (int i = 0; i < num; i++) {
            desc_binding[i].binding            = i;
            desc_binding[i].descriptorType     = desc[i].type;
            desc_binding[i].descriptorCount    = FFMAX(desc[i].elems, 1);
            desc_binding[i].stageFlags         = desc[i].stages;
            desc_binding[i].pImmutableSamplers = desc[i].sampler ?
                                                 desc[i].sampler->sampler :
                                                 NULL;
        }

        desc_create_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        desc_create_layout.pBindings = desc_binding;
        desc_create_layout.bindingCount = num;

        for (int i = 0; i < pl->qf->nb_queues; i++) {
            ret = vk->CreateDescriptorSetLayout(s->hwctx->act_dev, &desc_create_layout,
                                                s->hwctx->alloc, &layout[i]);
            if (ret != VK_SUCCESS) {
                av_log(s, AV_LOG_ERROR, "Unable to init descriptor set "
                       "layout: %s\n", ff_vk_ret2str(ret));
                av_free(desc_binding);
                return AVERROR_EXTERNAL;
            }
        }

        av_free(desc_binding);
    }

    { /* Pool each descriptor by type and update pool counts */
        for (int i = 0; i < num; i++) {
            int j;
            for (j = 0; j < pl->pool_size_desc_num; j++)
                if (pl->pool_size_desc[j].type == desc[i].type)
                    break;
            if (j >= pl->pool_size_desc_num) {
                pl->pool_size_desc = av_realloc_array(pl->pool_size_desc,
                                                      sizeof(*pl->pool_size_desc),
                                                      ++pl->pool_size_desc_num);
                if (!pl->pool_size_desc)
                    return AVERROR(ENOMEM);
                memset(&pl->pool_size_desc[j], 0, sizeof(VkDescriptorPoolSize));
            }
            pl->pool_size_desc[j].type             = desc[i].type;
            pl->pool_size_desc[j].descriptorCount += FFMAX(desc[i].elems, 1)*pl->qf->nb_queues;
        }
    }

    { /* Create template creation struct */
        VkDescriptorUpdateTemplateCreateInfo *dt;
        VkDescriptorUpdateTemplateEntry *des_entries;

        /* Freed after descriptor set initialization */
        des_entries = av_mallocz(num*sizeof(VkDescriptorUpdateTemplateEntry));
        if (!des_entries)
            return AVERROR(ENOMEM);

        for (int i = 0; i < num; i++) {
            des_entries[i].dstBinding      = i;
            des_entries[i].descriptorType  = desc[i].type;
            des_entries[i].descriptorCount = FFMAX(desc[i].elems, 1);
            des_entries[i].dstArrayElement = 0;
            des_entries[i].offset          = ((uint8_t *)desc[i].updater) - (uint8_t *)s;
            des_entries[i].stride          = descriptor_props[desc[i].type].struct_size;
        }

        pl->desc_template_info = av_realloc_array(pl->desc_template_info,
                                                  sizeof(*pl->desc_template_info),
                                                  pl->total_descriptor_sets + pl->qf->nb_queues);
        if (!pl->desc_template_info)
            return AVERROR(ENOMEM);

        dt = &pl->desc_template_info[pl->total_descriptor_sets];
        memset(dt, 0, sizeof(*dt)*pl->qf->nb_queues);

        for (int i = 0; i < pl->qf->nb_queues; i++) {
            dt[i].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO;
            dt[i].templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
            dt[i].descriptorSetLayout = layout[i];
            dt[i].pDescriptorUpdateEntries = des_entries;
            dt[i].descriptorUpdateEntryCount = num;
        }
    }

    pl->descriptor_sets_num++;

    pl->desc_layout_num += pl->qf->nb_queues;
    pl->total_descriptor_sets += pl->qf->nb_queues;

print:
    /* Write shader info */
    for (int i = 0; i < num; i++) {
        const struct descriptor_props *prop = &descriptor_props[desc[i].type];
        GLSLA("layout (set = %i, binding = %i", pl->descriptor_sets_num - 1, i);

        if (desc[i].mem_layout)
            GLSLA(", %s", desc[i].mem_layout);
        GLSLA(")");

        if (prop->is_uniform)
            GLSLA(" uniform");

        if (prop->mem_quali && desc[i].mem_quali)
            GLSLA(" %s", desc[i].mem_quali);

        if (prop->type)
            GLSLA(" %s", prop->type);

        if (prop->dim_needed)
            GLSLA("%iD", desc[i].dimensions);

        GLSLA(" %s", desc[i].name);

        if (prop->buf_content)
            GLSLA(" {\n    %s\n}", desc[i].buf_content);
        else if (desc[i].elems > 0)
            GLSLA("[%i]", desc[i].elems);

        GLSLA(";\n");
    }
    GLSLA("\n");

    return 0;
}

void ff_vk_update_descriptor_set(FFVulkanContext *s, FFVulkanPipeline *pl,
                                 int set_id)
{
    FFVulkanFunctions *vk = &s->vkfn;

    /* If a set has never been updated, update all queues' sets. */
    if (!pl->desc_set_initialized[set_id]) {
        for (int i = 0; i < pl->qf->nb_queues; i++) {
            int idx = set_id*pl->qf->nb_queues + i;
            vk->UpdateDescriptorSetWithTemplate(s->hwctx->act_dev,
                                                pl->desc_set[idx],
                                                pl->desc_template[idx],
                                                s);
        }
        pl->desc_set_initialized[set_id] = 1;
        return;
    }

    set_id = set_id*pl->qf->nb_queues + pl->qf->cur_queue;

    vk->UpdateDescriptorSetWithTemplate(s->hwctx->act_dev,
                                        pl->desc_set[set_id],
                                        pl->desc_template[set_id],
                                        s);
}

void ff_vk_update_push_exec(FFVulkanContext *s, FFVkExecContext *e,
                            VkShaderStageFlagBits stage, int offset,
                            size_t size, void *src)
{
    FFVulkanFunctions *vk = &s->vkfn;

    vk->CmdPushConstants(e->bufs[e->qf->cur_queue], e->bound_pl->pipeline_layout,
                         stage, offset, size, src);
}

int ff_vk_init_pipeline_layout(FFVulkanContext *s, FFVulkanPipeline *pl)
{
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;

    pl->desc_staging = av_malloc(pl->descriptor_sets_num*sizeof(*pl->desc_staging));
    if (!pl->desc_staging)
        return AVERROR(ENOMEM);

    { /* Init descriptor set pool */
        VkDescriptorPoolCreateInfo pool_create_info = {
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .poolSizeCount = pl->pool_size_desc_num,
            .pPoolSizes    = pl->pool_size_desc,
            .maxSets       = pl->total_descriptor_sets,
        };

        ret = vk->CreateDescriptorPool(s->hwctx->act_dev, &pool_create_info,
                                       s->hwctx->alloc, &pl->desc_pool);
        av_freep(&pl->pool_size_desc);
        if (ret != VK_SUCCESS) {
            av_log(s, AV_LOG_ERROR, "Unable to init descriptor set "
                   "pool: %s\n", ff_vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }
    }

    { /* Allocate descriptor sets */
        VkDescriptorSetAllocateInfo alloc_info = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = pl->desc_pool,
            .descriptorSetCount = pl->total_descriptor_sets,
            .pSetLayouts        = pl->desc_layout,
        };

        pl->desc_set = av_malloc(pl->total_descriptor_sets*sizeof(*pl->desc_set));
        if (!pl->desc_set)
            return AVERROR(ENOMEM);

        ret = vk->AllocateDescriptorSets(s->hwctx->act_dev, &alloc_info,
                                         pl->desc_set);
        if (ret != VK_SUCCESS) {
            av_log(s, AV_LOG_ERROR, "Unable to allocate descriptor set: %s\n",
                   ff_vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }
    }

    { /* Finally create the pipeline layout */
        VkPipelineLayoutCreateInfo spawn_pipeline_layout = {
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pSetLayouts            = (VkDescriptorSetLayout *)pl->desc_staging,
            .pushConstantRangeCount = pl->push_consts_num,
            .pPushConstantRanges    = pl->push_consts,
        };

        for (int i = 0; i < pl->total_descriptor_sets; i += pl->qf->nb_queues)
            pl->desc_staging[spawn_pipeline_layout.setLayoutCount++] = pl->desc_layout[i];

        ret = vk->CreatePipelineLayout(s->hwctx->act_dev, &spawn_pipeline_layout,
                                       s->hwctx->alloc, &pl->pipeline_layout);
        av_freep(&pl->push_consts);
        pl->push_consts_num = 0;
        if (ret != VK_SUCCESS) {
            av_log(s, AV_LOG_ERROR, "Unable to init pipeline layout: %s\n",
                   ff_vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }
    }

    { /* Descriptor template (for tightly packed descriptors) */
        VkDescriptorUpdateTemplateCreateInfo *dt;

        pl->desc_template = av_malloc(pl->total_descriptor_sets*sizeof(*pl->desc_template));
        if (!pl->desc_template)
            return AVERROR(ENOMEM);

        /* Create update templates for the descriptor sets */
        for (int i = 0; i < pl->total_descriptor_sets; i++) {
            dt = &pl->desc_template_info[i];
            dt->pipelineLayout = pl->pipeline_layout;
            ret = vk->CreateDescriptorUpdateTemplate(s->hwctx->act_dev,
                                                     dt, s->hwctx->alloc,
                                                     &pl->desc_template[i]);
            if (ret != VK_SUCCESS) {
                av_log(s, AV_LOG_ERROR, "Unable to init descriptor "
                       "template: %s\n", ff_vk_ret2str(ret));
                return AVERROR_EXTERNAL;
            }
        }

        /* Free the duplicated memory used for the template entries */
        for (int i = 0; i < pl->total_descriptor_sets; i += pl->qf->nb_queues) {
            dt = &pl->desc_template_info[i];
            av_free((void *)dt->pDescriptorUpdateEntries);
        }

        av_freep(&pl->desc_template_info);
    }

    return 0;
}

FN_CREATING(FFVulkanContext, FFVulkanPipeline, pipeline, pipelines, pipelines_num)
FFVulkanPipeline *ff_vk_create_pipeline(FFVulkanContext *s, FFVkQueueFamilyCtx *qf)
{
    FFVulkanPipeline *pl = create_pipeline(s);
    if (pl)
        pl->qf = qf;

    return pl;
}

int ff_vk_init_compute_pipeline(FFVulkanContext *s, FFVulkanPipeline *pl)
{
    int i;
    VkResult ret;
    FFVulkanFunctions *vk = &s->vkfn;

    VkComputePipelineCreateInfo pipe = {
        .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout = pl->pipeline_layout,
    };

    for (i = 0; i < pl->shaders_num; i++) {
        if (pl->shaders[i]->shader.stage & VK_SHADER_STAGE_COMPUTE_BIT) {
            pipe.stage = pl->shaders[i]->shader;
            break;
        }
    }
    if (i == pl->shaders_num) {
        av_log(s, AV_LOG_ERROR, "Can't init compute pipeline, no shader\n");
        return AVERROR(EINVAL);
    }

    ret = vk->CreateComputePipelines(s->hwctx->act_dev, VK_NULL_HANDLE, 1, &pipe,
                                     s->hwctx->alloc, &pl->pipeline);
    if (ret != VK_SUCCESS) {
        av_log(s, AV_LOG_ERROR, "Unable to init compute pipeline: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    pl->bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;

    return 0;
}

void ff_vk_bind_pipeline_exec(FFVulkanContext *s, FFVkExecContext *e,
                              FFVulkanPipeline *pl)
{
    FFVulkanFunctions *vk = &s->vkfn;

    vk->CmdBindPipeline(e->bufs[e->qf->cur_queue], pl->bind_point, pl->pipeline);

    for (int i = 0; i < pl->descriptor_sets_num; i++)
        pl->desc_staging[i] = pl->desc_set[i*pl->qf->nb_queues + pl->qf->cur_queue];

    vk->CmdBindDescriptorSets(e->bufs[e->qf->cur_queue], pl->bind_point,
                              pl->pipeline_layout, 0,
                              pl->descriptor_sets_num,
                              (VkDescriptorSet *)pl->desc_staging,
                              0, NULL);

    e->bound_pl = pl;
}

static void free_exec_ctx(FFVulkanContext *s, FFVkExecContext *e)
{
    FFVulkanFunctions *vk = &s->vkfn;

    /* Make sure all queues have finished executing */
    for (int i = 0; i < e->qf->nb_queues; i++) {
        FFVkQueueCtx *q = &e->queues[i];

        if (q->fence) {
            vk->WaitForFences(s->hwctx->act_dev, 1, &q->fence, VK_TRUE, UINT64_MAX);
            vk->ResetFences(s->hwctx->act_dev, 1, &q->fence);
        }

        /* Free the fence */
        if (q->fence)
            vk->DestroyFence(s->hwctx->act_dev, q->fence, s->hwctx->alloc);

        /* Free buffer dependencies */
        for (int j = 0; j < q->nb_buf_deps; j++)
            av_buffer_unref(&q->buf_deps[j]);
        av_free(q->buf_deps);

        /* Free frame dependencies */
        for (int j = 0; j < q->nb_frame_deps; j++)
            av_frame_free(&q->frame_deps[j]);
        av_free(q->frame_deps);
    }

    if (e->bufs)
        vk->FreeCommandBuffers(s->hwctx->act_dev, e->pool, e->qf->nb_queues, e->bufs);
    if (e->pool)
        vk->DestroyCommandPool(s->hwctx->act_dev, e->pool, s->hwctx->alloc);

    av_freep(&e->bufs);
    av_freep(&e->queues);
    av_freep(&e->sem_sig);
    av_freep(&e->sem_sig_val);
    av_freep(&e->sem_sig_val_dst);
    av_freep(&e->sem_wait);
    av_freep(&e->sem_wait_dst);
    av_freep(&e->sem_wait_val);
    av_free(e);
}

static void free_pipeline(FFVulkanContext *s, FFVulkanPipeline *pl)
{
    FFVulkanFunctions *vk = &s->vkfn;

    for (int i = 0; i < pl->shaders_num; i++) {
        FFVkSPIRVShader *shd = pl->shaders[i];
        av_bprint_finalize(&shd->src, NULL);
        vk->DestroyShaderModule(s->hwctx->act_dev, shd->shader.module,
                                s->hwctx->alloc);
        av_free(shd);
    }

    vk->DestroyPipeline(s->hwctx->act_dev, pl->pipeline, s->hwctx->alloc);
    vk->DestroyPipelineLayout(s->hwctx->act_dev, pl->pipeline_layout,
                              s->hwctx->alloc);

    for (int i = 0; i < pl->desc_layout_num; i++) {
        if (pl->desc_template && pl->desc_template[i])
            vk->DestroyDescriptorUpdateTemplate(s->hwctx->act_dev, pl->desc_template[i],
                                                s->hwctx->alloc);
        if (pl->desc_layout && pl->desc_layout[i])
            vk->DestroyDescriptorSetLayout(s->hwctx->act_dev, pl->desc_layout[i],
                                           s->hwctx->alloc);
    }

    /* Also frees the descriptor sets */
    if (pl->desc_pool)
        vk->DestroyDescriptorPool(s->hwctx->act_dev, pl->desc_pool,
                                  s->hwctx->alloc);

    av_freep(&pl->desc_staging);
    av_freep(&pl->desc_set);
    av_freep(&pl->shaders);
    av_freep(&pl->desc_layout);
    av_freep(&pl->desc_template);
    av_freep(&pl->desc_set_initialized);
    av_freep(&pl->push_consts);
    pl->push_consts_num = 0;

    /* Only freed in case of failure */
    av_freep(&pl->pool_size_desc);
    if (pl->desc_template_info) {
        for (int i = 0; i < pl->total_descriptor_sets; i += pl->qf->nb_queues) {
            VkDescriptorUpdateTemplateCreateInfo *dt = &pl->desc_template_info[i];
            av_free((void *)dt->pDescriptorUpdateEntries);
        }
        av_freep(&pl->desc_template_info);
    }

    av_free(pl);
}

void ff_vk_uninit(FFVulkanContext *s)
{
    FFVulkanFunctions *vk = &s->vkfn;

    if (s->spirv_compiler)
        s->spirv_compiler->uninit(&s->spirv_compiler);

    for (int i = 0; i < s->exec_ctx_num; i++)
        free_exec_ctx(s, s->exec_ctx[i]);
    av_freep(&s->exec_ctx);

    for (int i = 0; i < s->samplers_num; i++) {
        vk->DestroySampler(s->hwctx->act_dev, s->samplers[i]->sampler[0],
                           s->hwctx->alloc);
        av_free(s->samplers[i]);
    }
    av_freep(&s->samplers);

    for (int i = 0; i < s->pipelines_num; i++)
        free_pipeline(s, s->pipelines[i]);
    av_freep(&s->pipelines);

    av_freep(&s->scratch);
    s->scratch_size = 0;

    av_buffer_unref(&s->device_ref);
    av_buffer_unref(&s->frames_ref);
}
