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

#include "formats.h"
#include "vulkan.h"
#include "glslang.h"

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

static int vk_alloc_mem(AVFilterContext *avctx, VkMemoryRequirements *req,
                        VkMemoryPropertyFlagBits req_flags, void *alloc_extension,
                        VkMemoryPropertyFlagBits *mem_flags, VkDeviceMemory *mem)
{
    VkResult ret;
    int index = -1;
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceMemoryProperties mprops;
    VulkanFilterContext *s = avctx->priv;

    VkMemoryAllocateInfo alloc_info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext           = alloc_extension,
    };

    vkGetPhysicalDeviceProperties(s->hwctx->phys_dev, &props);
    vkGetPhysicalDeviceMemoryProperties(s->hwctx->phys_dev, &mprops);

    /* Align if we need to */
    if (req_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        req->size = FFALIGN(req->size, props.limits.minMemoryMapAlignment);

    alloc_info.allocationSize = req->size;

    /* The vulkan spec requires memory types to be sorted in the "optimal"
     * order, so the first matching type we find will be the best/fastest one */
    for (int i = 0; i < mprops.memoryTypeCount; i++) {
        /* The memory type must be supported by the requirements (bitfield) */
        if (!(req->memoryTypeBits & (1 << i)))
            continue;

        /* The memory type flags must include our properties */
        if ((mprops.memoryTypes[i].propertyFlags & req_flags) != req_flags)
            continue;

        /* Found a suitable memory type */
        index = i;
        break;
    }

    if (index < 0) {
        av_log(avctx, AV_LOG_ERROR, "No memory type found for flags 0x%x\n",
               req_flags);
        return AVERROR(EINVAL);
    }

    alloc_info.memoryTypeIndex = index;

    ret = vkAllocateMemory(s->hwctx->act_dev, &alloc_info,
                           s->hwctx->alloc, mem);
    if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate memory: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR(ENOMEM);
    }

    *mem_flags |= mprops.memoryTypes[index].propertyFlags;

    return 0;
}

int ff_vk_create_buf(AVFilterContext *avctx, FFVkBuffer *buf, size_t size,
                     VkBufferUsageFlags usage, VkMemoryPropertyFlagBits flags)
{
    int err;
    VkResult ret;
    int use_ded_mem;
    VulkanFilterContext *s = avctx->priv;

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

    ret = vkCreateBuffer(s->hwctx->act_dev, &buf_spawn, NULL, &buf->buf);
    if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create buffer: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    req_desc.buffer = buf->buf;

    vkGetBufferMemoryRequirements2(s->hwctx->act_dev, &req_desc, &req);

    /* In case the implementation prefers/requires dedicated allocation */
    use_ded_mem = ded_req.prefersDedicatedAllocation |
                  ded_req.requiresDedicatedAllocation;
    if (use_ded_mem)
        ded_alloc.buffer = buf->buf;

    err = vk_alloc_mem(avctx, &req.memoryRequirements, flags,
                       use_ded_mem ? &ded_alloc : (void *)ded_alloc.pNext,
                       &buf->flags, &buf->mem);
    if (err)
        return err;

    ret = vkBindBufferMemory(s->hwctx->act_dev, buf->buf, buf->mem, 0);
    if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to bind memory to buffer: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    return 0;
}

int ff_vk_map_buffers(AVFilterContext *avctx, FFVkBuffer *buf, uint8_t *mem[],
                      int nb_buffers, int invalidate)
{
    VkResult ret;
    VulkanFilterContext *s = avctx->priv;
    VkMappedMemoryRange *inval_list = NULL;
    int inval_count = 0;

    for (int i = 0; i < nb_buffers; i++) {
        ret = vkMapMemory(s->hwctx->act_dev, buf[i].mem, 0,
                          VK_WHOLE_SIZE, 0, (void **)&mem[i]);
        if (ret != VK_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to map buffer memory: %s\n",
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
        ret = vkInvalidateMappedMemoryRanges(s->hwctx->act_dev, inval_count,
                                             inval_list);
        if (ret != VK_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to invalidate memory: %s\n",
                   ff_vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }
    }

    return 0;
}

int ff_vk_unmap_buffers(AVFilterContext *avctx, FFVkBuffer *buf, int nb_buffers,
                        int flush)
{
    int err = 0;
    VkResult ret;
    VulkanFilterContext *s = avctx->priv;
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
        ret = vkFlushMappedMemoryRanges(s->hwctx->act_dev, flush_count,
                                        flush_list);
        if (ret != VK_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to flush memory: %s\n",
                   ff_vk_ret2str(ret));
            err = AVERROR_EXTERNAL; /* We still want to try to unmap them */
        }
    }

    for (int i = 0; i < nb_buffers; i++)
        vkUnmapMemory(s->hwctx->act_dev, buf[i].mem);

    return err;
}

void ff_vk_free_buf(AVFilterContext *avctx, FFVkBuffer *buf)
{
    VulkanFilterContext *s = avctx->priv;
    if (!buf)
        return;

    if (buf->buf != VK_NULL_HANDLE)
        vkDestroyBuffer(s->hwctx->act_dev, buf->buf, s->hwctx->alloc);
    if (buf->mem != VK_NULL_HANDLE)
        vkFreeMemory(s->hwctx->act_dev, buf->mem, s->hwctx->alloc);
}

int ff_vk_add_push_constant(AVFilterContext *avctx, VulkanPipeline *pl,
                            int offset, int size, VkShaderStageFlagBits stage)
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

FN_CREATING(VulkanFilterContext, FFVkExecContext, exec_ctx, exec_ctx, exec_ctx_num)
int ff_vk_create_exec_ctx(AVFilterContext *avctx, FFVkExecContext **ctx)
{
    VkResult ret;
    FFVkExecContext *e;
    VulkanFilterContext *s = avctx->priv;

    int queue_family = s->queue_family_idx;
    int nb_queues = s->queue_count;

    VkCommandPoolCreateInfo cqueue_create = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags              = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex   = queue_family,
    };
    VkCommandBufferAllocateInfo cbuf_create = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = nb_queues,
    };

    e = create_exec_ctx(s);
    if (!e)
        return AVERROR(ENOMEM);

    e->queues = av_mallocz(nb_queues * sizeof(*e->queues));
    if (!e->queues)
        return AVERROR(ENOMEM);

    e->bufs = av_mallocz(nb_queues * sizeof(*e->bufs));
    if (!e->bufs)
        return AVERROR(ENOMEM);

    /* Create command pool */
    ret = vkCreateCommandPool(s->hwctx->act_dev, &cqueue_create,
                              s->hwctx->alloc, &e->pool);
    if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Command pool creation failure: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    cbuf_create.commandPool = e->pool;

    /* Allocate command buffer */
    ret = vkAllocateCommandBuffers(s->hwctx->act_dev, &cbuf_create, e->bufs);
    if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Command buffer alloc failure: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    for (int i = 0; i < nb_queues; i++) {
        FFVkQueueCtx *q = &e->queues[i];
        vkGetDeviceQueue(s->hwctx->act_dev, queue_family, i, &q->queue);
    }

    *ctx = e;

    return 0;
}

void ff_vk_discard_exec_deps(AVFilterContext *avctx, FFVkExecContext *e)
{
    VulkanFilterContext *s = avctx->priv;
    FFVkQueueCtx *q = &e->queues[s->cur_queue_idx];

    for (int j = 0; j < q->nb_buf_deps; j++)
        av_buffer_unref(&q->buf_deps[j]);
    q->nb_buf_deps = 0;

    for (int j = 0; j < q->nb_frame_deps; j++)
        av_frame_free(&q->frame_deps[j]);
    q->nb_frame_deps = 0;

    e->sem_wait_cnt = 0;
    e->sem_sig_cnt = 0;
}

int ff_vk_start_exec_recording(AVFilterContext *avctx, FFVkExecContext *e)
{
    VkResult ret;
    VulkanFilterContext *s = avctx->priv;
    FFVkQueueCtx *q = &e->queues[s->cur_queue_idx];

    VkCommandBufferBeginInfo cmd_start = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    /* Create the fence and don't wait for it initially */
    if (!q->fence) {
        VkFenceCreateInfo fence_spawn = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        };
        ret = vkCreateFence(s->hwctx->act_dev, &fence_spawn, s->hwctx->alloc,
                            &q->fence);
        if (ret != VK_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to queue frame fence: %s\n",
                   ff_vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }
    } else {
        vkWaitForFences(s->hwctx->act_dev, 1, &q->fence, VK_TRUE, UINT64_MAX);
        vkResetFences(s->hwctx->act_dev, 1, &q->fence);
    }

    /* Discard queue dependencies */
    ff_vk_discard_exec_deps(avctx, e);

    ret = vkBeginCommandBuffer(e->bufs[s->cur_queue_idx], &cmd_start);
    if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to start command recoding: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    return 0;
}

VkCommandBuffer ff_vk_get_exec_buf(AVFilterContext *avctx, FFVkExecContext *e)
{
    VulkanFilterContext *s = avctx->priv;
    return e->bufs[s->cur_queue_idx];
}

int ff_vk_add_exec_dep(AVFilterContext *avctx, FFVkExecContext *e,
                       AVFrame *frame, VkPipelineStageFlagBits in_wait_dst_flag)
{
    AVFrame **dst;
    VulkanFilterContext *s = avctx->priv;
    AVVkFrame *f = (AVVkFrame *)frame->data[0];
    FFVkQueueCtx *q = &e->queues[s->cur_queue_idx];
    AVHWFramesContext *fc = (AVHWFramesContext *)frame->hw_frames_ctx->data;
    int planes = av_pix_fmt_count_planes(fc->sw_format);

    for (int i = 0; i < planes; i++) {
        e->sem_wait = av_fast_realloc(e->sem_wait, &e->sem_wait_alloc,
                                      (e->sem_wait_cnt + 1)*sizeof(*e->sem_wait));
        if (!e->sem_wait) {
            ff_vk_discard_exec_deps(avctx, e);
            return AVERROR(ENOMEM);
        }

        e->sem_wait_dst = av_fast_realloc(e->sem_wait_dst, &e->sem_wait_dst_alloc,
                                          (e->sem_wait_cnt + 1)*sizeof(*e->sem_wait_dst));
        if (!e->sem_wait_dst) {
            ff_vk_discard_exec_deps(avctx, e);
            return AVERROR(ENOMEM);
        }

        e->sem_sig = av_fast_realloc(e->sem_sig, &e->sem_sig_alloc,
                                     (e->sem_sig_cnt + 1)*sizeof(*e->sem_sig));
        if (!e->sem_sig) {
            ff_vk_discard_exec_deps(avctx, e);
            return AVERROR(ENOMEM);
        }

        e->sem_wait[e->sem_wait_cnt] = f->sem[i];
        e->sem_wait_dst[e->sem_wait_cnt] = in_wait_dst_flag;
        e->sem_wait_cnt++;

        e->sem_sig[e->sem_sig_cnt] = f->sem[i];
        e->sem_sig_cnt++;
    }

    dst = av_fast_realloc(q->frame_deps, &q->frame_deps_alloc_size,
                          (q->nb_frame_deps + 1) * sizeof(*dst));
    if (!dst) {
        ff_vk_discard_exec_deps(avctx, e);
        return AVERROR(ENOMEM);
    }

    q->frame_deps = dst;
    q->frame_deps[q->nb_frame_deps] = av_frame_clone(frame);
    if (!q->frame_deps[q->nb_frame_deps]) {
        ff_vk_discard_exec_deps(avctx, e);
        return AVERROR(ENOMEM);
    }
    q->nb_frame_deps++;

    return 0;
}

int ff_vk_submit_exec_queue(AVFilterContext *avctx, FFVkExecContext *e)
{
    VkResult ret;
    VulkanFilterContext *s = avctx->priv;
    FFVkQueueCtx *q = &e->queues[s->cur_queue_idx];

    VkSubmitInfo s_info = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &e->bufs[s->cur_queue_idx],

        .pWaitSemaphores      = e->sem_wait,
        .pWaitDstStageMask    = e->sem_wait_dst,
        .waitSemaphoreCount   = e->sem_wait_cnt,

        .pSignalSemaphores    = e->sem_sig,
        .signalSemaphoreCount = e->sem_sig_cnt,
    };

    ret = vkEndCommandBuffer(e->bufs[s->cur_queue_idx]);
    if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Unable to finish command buffer: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    ret = vkQueueSubmit(q->queue, 1, &s_info, q->fence);
    if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Unable to submit command buffer: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    /* Rotate queues */
    s->cur_queue_idx = (s->cur_queue_idx + 1) % s->queue_count;

    return 0;
}

int ff_vk_add_dep_exec_ctx(AVFilterContext *avctx, FFVkExecContext *e,
                           AVBufferRef **deps, int nb_deps)
{
    AVBufferRef **dst;
    VulkanFilterContext *s = avctx->priv;
    FFVkQueueCtx *q = &e->queues[s->cur_queue_idx];

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
    ff_vk_discard_exec_deps(avctx, e);
    return AVERROR(ENOMEM);
}

int ff_vk_filter_query_formats(AVFilterContext *avctx)
{
    static const enum AVPixelFormat pixel_formats[] = {
        AV_PIX_FMT_VULKAN, AV_PIX_FMT_NONE,
    };
    AVFilterFormats *pix_fmts = ff_make_format_list(pixel_formats);
    if (!pix_fmts)
        return AVERROR(ENOMEM);

    return ff_set_common_formats(avctx, pix_fmts);
}

static int vulkan_filter_set_device(AVFilterContext *avctx,
                                    AVBufferRef *device)
{
    VulkanFilterContext *s = avctx->priv;

    av_buffer_unref(&s->device_ref);

    s->device_ref = av_buffer_ref(device);
    if (!s->device_ref)
        return AVERROR(ENOMEM);

    s->device = (AVHWDeviceContext*)s->device_ref->data;
    s->hwctx  = s->device->hwctx;

    return 0;
}

static int vulkan_filter_set_frames(AVFilterContext *avctx,
                                    AVBufferRef *frames)
{
    VulkanFilterContext *s = avctx->priv;

    av_buffer_unref(&s->frames_ref);

    s->frames_ref = av_buffer_ref(frames);
    if (!s->frames_ref)
        return AVERROR(ENOMEM);

    return 0;
}

int ff_vk_filter_config_input(AVFilterLink *inlink)
{
    int err;
    AVFilterContext *avctx = inlink->dst;
    VulkanFilterContext *s = avctx->priv;
    AVHWFramesContext *input_frames;

    if (!inlink->hw_frames_ctx) {
        av_log(avctx, AV_LOG_ERROR, "Vulkan filtering requires a "
               "hardware frames context on the input.\n");
        return AVERROR(EINVAL);
    }

    /* Extract the device and default output format from the first input. */
    if (avctx->inputs[0] != inlink)
        return 0;

    input_frames = (AVHWFramesContext*)inlink->hw_frames_ctx->data;
    if (input_frames->format != AV_PIX_FMT_VULKAN)
        return AVERROR(EINVAL);

    err = vulkan_filter_set_device(avctx, input_frames->device_ref);
    if (err < 0)
        return err;
    err = vulkan_filter_set_frames(avctx, inlink->hw_frames_ctx);
    if (err < 0)
        return err;

    /* Default output parameters match input parameters. */
    s->input_format = input_frames->sw_format;
    if (s->output_format == AV_PIX_FMT_NONE)
        s->output_format = input_frames->sw_format;
    if (!s->output_width)
        s->output_width  = inlink->w;
    if (!s->output_height)
        s->output_height = inlink->h;

    return 0;
}

int ff_vk_filter_config_output_inplace(AVFilterLink *outlink)
{
    int err;
    AVFilterContext *avctx = outlink->src;
    VulkanFilterContext *s = avctx->priv;

    av_buffer_unref(&outlink->hw_frames_ctx);

    if (!s->device_ref) {
        if (!avctx->hw_device_ctx) {
            av_log(avctx, AV_LOG_ERROR, "Vulkan filtering requires a "
                   "Vulkan device.\n");
            return AVERROR(EINVAL);
        }

        err = vulkan_filter_set_device(avctx, avctx->hw_device_ctx);
        if (err < 0)
            return err;
    }

    outlink->hw_frames_ctx = av_buffer_ref(s->frames_ref);
    if (!outlink->hw_frames_ctx)
        return AVERROR(ENOMEM);

    outlink->w = s->output_width;
    outlink->h = s->output_height;

    return 0;
}

int ff_vk_filter_config_output(AVFilterLink *outlink)
{
    int err;
    AVFilterContext *avctx = outlink->src;
    VulkanFilterContext *s = avctx->priv;
    AVBufferRef *output_frames_ref;
    AVHWFramesContext *output_frames;

    av_buffer_unref(&outlink->hw_frames_ctx);

    if (!s->device_ref) {
        if (!avctx->hw_device_ctx) {
            av_log(avctx, AV_LOG_ERROR, "Vulkan filtering requires a "
                   "Vulkan device.\n");
            return AVERROR(EINVAL);
        }

        err = vulkan_filter_set_device(avctx, avctx->hw_device_ctx);
        if (err < 0)
            return err;
    }

    output_frames_ref = av_hwframe_ctx_alloc(s->device_ref);
    if (!output_frames_ref) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    output_frames = (AVHWFramesContext*)output_frames_ref->data;

    output_frames->format    = AV_PIX_FMT_VULKAN;
    output_frames->sw_format = s->output_format;
    output_frames->width     = s->output_width;
    output_frames->height    = s->output_height;

    err = av_hwframe_ctx_init(output_frames_ref);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialise output "
               "frames: %d.\n", err);
        goto fail;
    }

    outlink->hw_frames_ctx = output_frames_ref;
    outlink->w = s->output_width;
    outlink->h = s->output_height;

    return 0;
fail:
    av_buffer_unref(&output_frames_ref);
    return err;
}

int ff_vk_filter_init(AVFilterContext *avctx)
{
    VulkanFilterContext *s = avctx->priv;

    s->output_format = AV_PIX_FMT_NONE;

    if (glslang_init())
        return AVERROR_EXTERNAL;

    return 0;
}

FN_CREATING(VulkanFilterContext, VkSampler, sampler, samplers, samplers_num)
VkSampler *ff_vk_init_sampler(AVFilterContext *avctx, int unnorm_coords,
                              VkFilter filt)
{
    VkResult ret;
    VulkanFilterContext *s = avctx->priv;

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

    VkSampler *sampler = create_sampler(s);
    if (!sampler)
        return NULL;

    ret = vkCreateSampler(s->hwctx->act_dev, &sampler_info,
                          s->hwctx->alloc, sampler);
    if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Unable to init sampler: %s\n",
               ff_vk_ret2str(ret));
        return NULL;
    }

    return sampler;
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
    VulkanFilterContext *s = opaque;
    ImageViewCtx *iv = (ImageViewCtx *)data;
    vkDestroyImageView(s->hwctx->act_dev, iv->view, s->hwctx->alloc);
    av_free(iv);
}

int ff_vk_create_imageview(AVFilterContext *avctx, FFVkExecContext *e,
                           VkImageView *v, VkImage img, VkFormat fmt,
                           const VkComponentMapping map)
{
    int err;
    AVBufferRef *buf;
    VulkanFilterContext *s = avctx->priv;
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

    VkResult ret = vkCreateImageView(s->hwctx->act_dev, &imgview_spawn,
                                     s->hwctx->alloc, &iv->view);
    if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create imageview: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    buf = av_buffer_create((uint8_t *)iv, sizeof(*iv), destroy_imageview, s, 0);
    if (!buf) {
        destroy_imageview(s, (uint8_t *)iv);
        return AVERROR(ENOMEM);
    }

    /* Add to queue dependencies */
    err = ff_vk_add_dep_exec_ctx(avctx, e, &buf, 1);
    if (err) {
        av_buffer_unref(&buf);
        return err;
    }

    *v = iv->view;

    return 0;
}

FN_CREATING(VulkanPipeline, SPIRVShader, shader, shaders, shaders_num)
SPIRVShader *ff_vk_init_shader(AVFilterContext *avctx, VulkanPipeline *pl,
                               const char *name, VkShaderStageFlags stage)
{
    SPIRVShader *shd = create_shader(pl);
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

void ff_vk_set_compute_shader_sizes(AVFilterContext *avctx, SPIRVShader *shd,
                                        int local_size[3])
{
    shd->local_size[0] = local_size[0];
    shd->local_size[1] = local_size[1];
    shd->local_size[2] = local_size[2];

    av_bprintf(&shd->src, "layout (local_size_x = %i, "
               "local_size_y = %i, local_size_z = %i) in;\n\n",
               shd->local_size[0], shd->local_size[1], shd->local_size[2]);
}

static void print_shader(AVFilterContext *avctx, SPIRVShader *shd, int prio)
{
    int line = 0;
    const char *p = shd->src.str;
    const char *start = p;

    AVBPrint buf;
    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    for (int i = 0; i < strlen(p); i++) {
        if (p[i] == '\n') {
            av_bprintf(&buf, "%i\t", ++line);
            av_bprint_append_data(&buf, start, &p[i] - start + 1);
            start = &p[i + 1];
        }
    }

    av_log(avctx, prio, "Shader %s: \n%s", shd->name, buf.str);
    av_bprint_finalize(&buf, NULL);
}

int ff_vk_compile_shader(AVFilterContext *avctx, SPIRVShader *shd,
                         const char *entrypoint)
{
    VkResult ret;
    VulkanFilterContext *s = avctx->priv;
    VkShaderModuleCreateInfo shader_create;
    GLSlangResult *res;

    static const enum GLSlangStage emap[] = {
        [VK_SHADER_STAGE_VERTEX_BIT]   = GLSLANG_VERTEX,
        [VK_SHADER_STAGE_FRAGMENT_BIT] = GLSLANG_FRAGMENT,
        [VK_SHADER_STAGE_COMPUTE_BIT]  = GLSLANG_COMPUTE,
    };

    shd->shader.pName = entrypoint;

    res = glslang_compile(shd->src.str, emap[shd->shader.stage]);
    if (!res)
        return AVERROR(ENOMEM);

    if (res->rval) {
        av_log(avctx, AV_LOG_ERROR, "Error compiling shader %s: %s!\n",
               shd->name, av_err2str(res->rval));
        print_shader(avctx, shd, AV_LOG_ERROR);
        if (res->error_msg)
            av_log(avctx, AV_LOG_ERROR, "%s", res->error_msg);
        av_free(res->error_msg);
        return res->rval;
    }

    print_shader(avctx, shd, AV_LOG_VERBOSE);

    shader_create.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_create.pNext    = NULL;
    shader_create.codeSize = res->size;
    shader_create.flags    = 0;
    shader_create.pCode    = res->data;

    ret = vkCreateShaderModule(s->hwctx->act_dev, &shader_create, NULL,
                               &shd->shader.module);

    /* Free the GLSlangResult struct */
    av_free(res->data);
    av_free(res);

    if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Unable to create shader module: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    av_log(avctx, AV_LOG_VERBOSE, "Shader %s linked! Size: %zu bytes\n",
           shd->name, shader_create.codeSize);

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

int ff_vk_add_descriptor_set(AVFilterContext *avctx, VulkanPipeline *pl,
                             SPIRVShader *shd, VulkanDescriptorSetBinding *desc,
                             int num, int only_print_to_shader)
{
    VkResult ret;
    VkDescriptorSetLayout *layout;
    VulkanFilterContext *s = avctx->priv;

    if (only_print_to_shader)
        goto print;

    pl->desc_layout = av_realloc_array(pl->desc_layout, sizeof(*pl->desc_layout),
                                       pl->desc_layout_num + 1);
    if (!pl->desc_layout)
        return AVERROR(ENOMEM);

    layout = &pl->desc_layout[pl->desc_layout_num];
    memset(layout, 0, sizeof(*layout));

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
            desc_binding[i].pImmutableSamplers = desc[i].samplers;
        }

        desc_create_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        desc_create_layout.pBindings = desc_binding;
        desc_create_layout.bindingCount = num;

        ret = vkCreateDescriptorSetLayout(s->hwctx->act_dev, &desc_create_layout,
                                          s->hwctx->alloc, layout);
        av_free(desc_binding);
        if (ret != VK_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Unable to init descriptor set "
                   "layout: %s\n", ff_vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }
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
            pl->pool_size_desc[j].descriptorCount += FFMAX(desc[i].elems, 1);
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
                                                  pl->desc_layout_num + 1);
        if (!pl->desc_template_info)
            return AVERROR(ENOMEM);

        dt = &pl->desc_template_info[pl->desc_layout_num];
        memset(dt, 0, sizeof(*dt));

        dt->sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO;
        dt->templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
        dt->descriptorSetLayout = *layout;
        dt->pDescriptorUpdateEntries = des_entries;
        dt->descriptorUpdateEntryCount = num;
    }

    pl->desc_layout_num++;

print:
    /* Write shader info */
    for (int i = 0; i < num; i++) {
        const struct descriptor_props *prop = &descriptor_props[desc[i].type];
        GLSLA("layout (set = %i, binding = %i", pl->desc_layout_num - 1, i);

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

void ff_vk_update_descriptor_set(AVFilterContext *avctx, VulkanPipeline *pl,
                                 int set_id)
{
    VulkanFilterContext *s = avctx->priv;

    vkUpdateDescriptorSetWithTemplate(s->hwctx->act_dev,
                                      pl->desc_set[s->cur_queue_idx * pl->desc_layout_num + set_id],
                                      pl->desc_template[set_id],
                                      s);
}

void ff_vk_update_push_exec(AVFilterContext *avctx, FFVkExecContext *e,
                            VkShaderStageFlagBits stage, int offset,
                            size_t size, void *src)
{
    VulkanFilterContext *s = avctx->priv;
    vkCmdPushConstants(e->bufs[s->cur_queue_idx], e->bound_pl->pipeline_layout,
                       stage, offset, size, src);
}

int ff_vk_init_pipeline_layout(AVFilterContext *avctx, VulkanPipeline *pl)
{
    VkResult ret;
    VulkanFilterContext *s = avctx->priv;

    pl->descriptor_sets_num = pl->desc_layout_num * s->queue_count;

    { /* Init descriptor set pool */
        VkDescriptorPoolCreateInfo pool_create_info = {
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .poolSizeCount = pl->pool_size_desc_num,
            .pPoolSizes    = pl->pool_size_desc,
            .maxSets       = pl->descriptor_sets_num,
        };

        ret = vkCreateDescriptorPool(s->hwctx->act_dev, &pool_create_info,
                                     s->hwctx->alloc, &pl->desc_pool);
        av_freep(&pl->pool_size_desc);
        if (ret != VK_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Unable to init descriptor set "
                   "pool: %s\n", ff_vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }
    }

    { /* Allocate descriptor sets */
        VkDescriptorSetAllocateInfo alloc_info = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = pl->desc_pool,
            .descriptorSetCount = pl->descriptor_sets_num,
            .pSetLayouts        = pl->desc_layout,
        };

        pl->desc_set = av_malloc(pl->descriptor_sets_num*sizeof(*pl->desc_set));
        if (!pl->desc_set)
            return AVERROR(ENOMEM);

        ret = vkAllocateDescriptorSets(s->hwctx->act_dev, &alloc_info,
                                       pl->desc_set);
        if (ret != VK_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Unable to allocate descriptor set: %s\n",
                   ff_vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }
    }

    { /* Finally create the pipeline layout */
        VkPipelineLayoutCreateInfo spawn_pipeline_layout = {
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount         = pl->desc_layout_num,
            .pSetLayouts            = pl->desc_layout,
            .pushConstantRangeCount = pl->push_consts_num,
            .pPushConstantRanges    = pl->push_consts,
        };

        ret = vkCreatePipelineLayout(s->hwctx->act_dev, &spawn_pipeline_layout,
                                     s->hwctx->alloc, &pl->pipeline_layout);
        av_freep(&pl->push_consts);
        pl->push_consts_num = 0;
        if (ret != VK_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Unable to init pipeline layout: %s\n",
                   ff_vk_ret2str(ret));
            return AVERROR_EXTERNAL;
        }
    }

    { /* Descriptor template (for tightly packed descriptors) */
        VkDescriptorUpdateTemplateCreateInfo *desc_template_info;

        pl->desc_template = av_malloc(pl->descriptor_sets_num*sizeof(*pl->desc_template));
        if (!pl->desc_template)
            return AVERROR(ENOMEM);

        /* Create update templates for the descriptor sets */
        for (int i = 0; i < pl->descriptor_sets_num; i++) {
            desc_template_info = &pl->desc_template_info[i % pl->desc_layout_num];
            desc_template_info->pipelineLayout = pl->pipeline_layout;
            ret = vkCreateDescriptorUpdateTemplate(s->hwctx->act_dev,
                                                   desc_template_info,
                                                   s->hwctx->alloc,
                                                   &pl->desc_template[i]);
            av_free((void *)desc_template_info->pDescriptorUpdateEntries);
            if (ret != VK_SUCCESS) {
                av_log(avctx, AV_LOG_ERROR, "Unable to init descriptor "
                       "template: %s\n", ff_vk_ret2str(ret));
                return AVERROR_EXTERNAL;
            }
        }

        av_freep(&pl->desc_template_info);
    }

    return 0;
}

FN_CREATING(VulkanFilterContext, VulkanPipeline, pipeline, pipelines, pipelines_num)
VulkanPipeline *ff_vk_create_pipeline(AVFilterContext *avctx)
{
    return create_pipeline(avctx->priv);
}

int ff_vk_init_compute_pipeline(AVFilterContext *avctx, VulkanPipeline *pl)
{
    int i;
    VkResult ret;
    VulkanFilterContext *s = avctx->priv;

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
        av_log(avctx, AV_LOG_ERROR, "Can't init compute pipeline, no shader\n");
        return AVERROR(EINVAL);
    }

    ret = vkCreateComputePipelines(s->hwctx->act_dev, VK_NULL_HANDLE, 1, &pipe,
                                   s->hwctx->alloc, &pl->pipeline);
    if (ret != VK_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Unable to init compute pipeline: %s\n",
               ff_vk_ret2str(ret));
        return AVERROR_EXTERNAL;
    }

    pl->bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;

    return 0;
}

void ff_vk_bind_pipeline_exec(AVFilterContext *avctx, FFVkExecContext *e,
                              VulkanPipeline *pl)
{
    VulkanFilterContext *s = avctx->priv;

    vkCmdBindPipeline(e->bufs[s->cur_queue_idx], pl->bind_point, pl->pipeline);

    vkCmdBindDescriptorSets(e->bufs[s->cur_queue_idx], pl->bind_point,
                            pl->pipeline_layout, 0, pl->descriptor_sets_num,
                            pl->desc_set, 0, 0);

    e->bound_pl = pl;
}

static void free_exec_ctx(VulkanFilterContext *s, FFVkExecContext *e)
{
    /* Make sure all queues have finished executing */
    for (int i = 0; i < s->queue_count; i++) {
        FFVkQueueCtx *q = &e->queues[i];

        if (q->fence) {
            vkWaitForFences(s->hwctx->act_dev, 1, &q->fence, VK_TRUE, UINT64_MAX);
            vkResetFences(s->hwctx->act_dev, 1, &q->fence);
        }

        /* Free the fence */
        if (q->fence)
            vkDestroyFence(s->hwctx->act_dev, q->fence, s->hwctx->alloc);

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
        vkFreeCommandBuffers(s->hwctx->act_dev, e->pool, s->queue_count, e->bufs);
    if (e->pool)
        vkDestroyCommandPool(s->hwctx->act_dev, e->pool, s->hwctx->alloc);

    av_freep(&e->bufs);
    av_freep(&e->queues);
    av_freep(&e->sem_sig);
    av_freep(&e->sem_wait);
    av_freep(&e->sem_wait_dst);
    av_free(e);
}

static void free_pipeline(VulkanFilterContext *s, VulkanPipeline *pl)
{
    for (int i = 0; i < pl->shaders_num; i++) {
        SPIRVShader *shd = pl->shaders[i];
        av_bprint_finalize(&shd->src, NULL);
        vkDestroyShaderModule(s->hwctx->act_dev, shd->shader.module,
                              s->hwctx->alloc);
        av_free(shd);
    }

    vkDestroyPipeline(s->hwctx->act_dev, pl->pipeline, s->hwctx->alloc);
    vkDestroyPipelineLayout(s->hwctx->act_dev, pl->pipeline_layout,
                            s->hwctx->alloc);

    for (int i = 0; i < pl->desc_layout_num; i++) {
        if (pl->desc_template && pl->desc_template[i])
            vkDestroyDescriptorUpdateTemplate(s->hwctx->act_dev, pl->desc_template[i],
                                              s->hwctx->alloc);
        if (pl->desc_layout && pl->desc_layout[i])
            vkDestroyDescriptorSetLayout(s->hwctx->act_dev, pl->desc_layout[i],
                                         s->hwctx->alloc);
    }

    /* Also frees the descriptor sets */
    if (pl->desc_pool)
        vkDestroyDescriptorPool(s->hwctx->act_dev, pl->desc_pool,
                                s->hwctx->alloc);

    av_freep(&pl->desc_set);
    av_freep(&pl->shaders);
    av_freep(&pl->desc_layout);
    av_freep(&pl->desc_template);
    av_freep(&pl->push_consts);
    pl->push_consts_num = 0;

    /* Only freed in case of failure */
    av_freep(&pl->pool_size_desc);
    if (pl->desc_template_info) {
        for (int i = 0; i < pl->descriptor_sets_num; i++)
            av_free((void *)pl->desc_template_info[i].pDescriptorUpdateEntries);
        av_freep(&pl->desc_template_info);
    }

    av_free(pl);
}

void ff_vk_filter_uninit(AVFilterContext *avctx)
{
    VulkanFilterContext *s = avctx->priv;

    glslang_uninit();

    for (int i = 0; i < s->exec_ctx_num; i++)
        free_exec_ctx(s, s->exec_ctx[i]);
    av_freep(&s->exec_ctx);

    for (int i = 0; i < s->samplers_num; i++) {
        vkDestroySampler(s->hwctx->act_dev, *s->samplers[i], s->hwctx->alloc);
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
