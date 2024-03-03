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

#ifndef AVUTIL_VULKAN_H
#define AVUTIL_VULKAN_H

#define VK_NO_PROTOTYPES

#include <stdatomic.h>

#include "thread.h"
#include "pixdesc.h"
#include "bprint.h"
#include "hwcontext.h"
#include "vulkan_functions.h"
#include "hwcontext_vulkan.h"

/* GLSL management macros */
#define INDENT(N) INDENT_##N
#define INDENT_0
#define INDENT_1 INDENT_0 "    "
#define INDENT_2 INDENT_1 INDENT_1
#define INDENT_3 INDENT_2 INDENT_1
#define INDENT_4 INDENT_3 INDENT_1
#define INDENT_5 INDENT_4 INDENT_1
#define INDENT_6 INDENT_5 INDENT_1
#define C(N, S)          INDENT(N) #S "\n"

#define GLSLC(N, S)                     \
    do {                                \
        av_bprintf(&shd->src, C(N, S)); \
    } while (0)

#define GLSLA(...)                          \
    do {                                    \
        av_bprintf(&shd->src, __VA_ARGS__); \
    } while (0)

#define GLSLF(N, S, ...)                             \
    do {                                             \
        av_bprintf(&shd->src, C(N, S), __VA_ARGS__); \
    } while (0)

#define GLSLD(D)                                        \
    do {                                                \
        av_bprintf(&shd->src, "\n");                    \
        av_bprint_append_data(&shd->src, D, strlen(D)); \
        av_bprintf(&shd->src, "\n");                    \
    } while (0)

/* Helper, pretty much every Vulkan return value needs to be checked */
#define RET(x)                                                                 \
    do {                                                                       \
        if ((err = (x)) < 0)                                                   \
            goto fail;                                                         \
    } while (0)

#define DUP_SAMPLER(x) { x, x, x, x }

typedef struct FFVkSPIRVShader {
    const char *name;                       /* Name for id/debugging purposes */
    AVBPrint src;
    int local_size[3];                      /* Compute shader workgroup sizes */
    VkPipelineShaderStageCreateInfo shader;
    VkPipelineShaderStageRequiredSubgroupSizeCreateInfo subgroup_info;
} FFVkSPIRVShader;

typedef struct FFVulkanDescriptorSetBinding {
    const char         *name;
    VkDescriptorType    type;
    const char         *mem_layout;  /* Storage images (rgba8, etc.) and buffers (std430, etc.) */
    const char         *mem_quali;   /* readonly, writeonly, etc. */
    const char         *buf_content; /* For buffers */
    uint32_t            dimensions;  /* Needed for e.g. sampler%iD */
    uint32_t            elems;       /* 0 - scalar, 1 or more - vector */
    VkShaderStageFlags  stages;
    VkSampler           samplers[4]; /* Sampler to use for all elems */
} FFVulkanDescriptorSetBinding;

typedef struct FFVkBuffer {
    VkBuffer buf;
    VkDeviceMemory mem;
    VkMemoryPropertyFlagBits flags;
    size_t size;
    VkDeviceAddress address;

    /* Local use only */
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;

    /* Only valid when allocated via ff_vk_get_pooled_buffer with HOST_VISIBLE */
    uint8_t *mapped_mem;
} FFVkBuffer;

typedef struct FFVkQueueFamilyCtx {
    int queue_family;
    int nb_queues;
} FFVkQueueFamilyCtx;

typedef struct FFVulkanDescriptorSet {
    VkDescriptorSetLayout  layout;
    FFVkBuffer             buf;
    uint8_t               *desc_mem;
    VkDeviceSize           layout_size;
    VkDeviceSize           aligned_size; /* descriptorBufferOffsetAlignment */
    VkDeviceSize           total_size; /* Once registered to an exec context */
    VkBufferUsageFlags     usage;

    VkDescriptorSetLayoutBinding *binding;
    VkDeviceSize *binding_offset;
    int nb_bindings;

    int read_only;
} FFVulkanDescriptorSet;

typedef struct FFVulkanPipeline {
    VkPipelineBindPoint bind_point;

    /* Contexts */
    VkPipelineLayout pipeline_layout;
    VkPipeline       pipeline;

    /* Push consts */
    VkPushConstantRange *push_consts;
    int push_consts_num;

    /* Workgroup */
    int wg_size[3];

    /* Descriptors */
    FFVulkanDescriptorSet *desc_set;
    VkDescriptorBufferBindingInfoEXT *desc_bind;
    uint32_t *bound_buffer_indices;
    int nb_descriptor_sets;
} FFVulkanPipeline;

typedef struct FFVkExecContext {
    uint32_t idx;
    const struct FFVkExecPool *parent;
    pthread_mutex_t lock;
    int had_submission;

    /* Queue for the execution context */
    VkQueue queue;
    int qf;
    int qi;

    /* Command buffer for the context */
    VkCommandBuffer buf;

    /* Fence for the command buffer */
    VkFence fence;

    void *query_data;
    int query_idx;

    /* Buffer dependencies */
    AVBufferRef **buf_deps;
    int nb_buf_deps;
    unsigned int buf_deps_alloc_size;

    /* Frame dependencies */
    AVFrame **frame_deps;
    unsigned int frame_deps_alloc_size;
    int nb_frame_deps;

    VkSemaphoreSubmitInfo *sem_wait;
    unsigned int sem_wait_alloc;
    int sem_wait_cnt;

    VkSemaphoreSubmitInfo *sem_sig;
    unsigned int sem_sig_alloc;
    int sem_sig_cnt;

    uint64_t **sem_sig_val_dst;
    unsigned int sem_sig_val_dst_alloc;
    int sem_sig_val_dst_cnt;

    uint8_t *frame_locked;
    unsigned int frame_locked_alloc_size;

    VkAccessFlagBits *access_dst;
    unsigned int access_dst_alloc;

    VkImageLayout *layout_dst;
    unsigned int layout_dst_alloc;

    uint32_t *queue_family_dst;
    unsigned int queue_family_dst_alloc;

    uint8_t *frame_update;
    unsigned int frame_update_alloc_size;
} FFVkExecContext;

typedef struct FFVkExecPool {
    FFVkExecContext *contexts;
    atomic_int_least64_t idx;

    VkCommandPool cmd_buf_pool;
    VkCommandBuffer *cmd_bufs;
    int pool_size;

    VkQueryPool query_pool;
    void *query_data;
    int query_results;
    int query_statuses;
    int query_64bit;
    int query_status_stride;
    int nb_queries;
    size_t qd_size;
} FFVkExecPool;

typedef struct FFVulkanContext {
    const AVClass *class; /* Filters and encoders use this */

    FFVulkanFunctions     vkfn;
    FFVulkanExtensions    extensions;
    VkPhysicalDeviceProperties2 props;
    VkPhysicalDeviceDriverProperties driver_props;
    VkPhysicalDeviceMemoryProperties mprops;
    VkPhysicalDeviceExternalMemoryHostPropertiesEXT hprops;
    VkPhysicalDeviceDescriptorBufferPropertiesEXT desc_buf_props;
    VkPhysicalDeviceSubgroupSizeControlProperties subgroup_props;
    VkPhysicalDeviceCooperativeMatrixPropertiesKHR coop_matrix_props;
    VkQueueFamilyQueryResultStatusPropertiesKHR *query_props;
    VkQueueFamilyVideoPropertiesKHR *video_props;
    VkQueueFamilyProperties2 *qf_props;
    int tot_nb_qfs;

    VkCooperativeMatrixPropertiesKHR *coop_mat_props;
    uint32_t coop_mat_props_nb;

    VkPhysicalDeviceShaderAtomicFloatFeaturesEXT atomic_float_feats;
    VkPhysicalDeviceVulkan12Features feats_12;
    VkPhysicalDeviceFeatures2 feats;

    AVHWDeviceContext     *device;
    AVVulkanDeviceContext *hwctx;

    AVBufferRef           *input_frames_ref;
    AVBufferRef           *frames_ref;
    AVHWFramesContext     *frames;
    AVVulkanFramesContext *hwfc;

    uint32_t               qfs[5];
    int                    nb_qfs;

    /* Properties */
    int                 output_width;
    int                output_height;
    enum AVPixelFormat output_format;
    enum AVPixelFormat  input_format;
} FFVulkanContext;

static inline int ff_vk_count_images(AVVkFrame *f)
{
    int cnt = 0;
    while (cnt < FF_ARRAY_ELEMS(f->img) && f->img[cnt])
        cnt++;

    return cnt;
}

static inline const void *ff_vk_find_struct(const void *chain, VkStructureType stype)
{
    const VkBaseInStructure *in = chain;
    while (in) {
        if (in->sType == stype)
            return in;

        in = in->pNext;
    }

    return NULL;
}

/* Identity mapping - r = r, b = b, g = g, a = a */
extern const VkComponentMapping ff_comp_identity_map;

/**
 * Converts Vulkan return values to strings
 */
const char *ff_vk_ret2str(VkResult res);

/**
 * Returns 1 if pixfmt is a usable RGB format.
 */
int ff_vk_mt_is_np_rgb(enum AVPixelFormat pix_fmt);

/**
 * Returns the format to use for images in shaders.
 */
const char *ff_vk_shader_rep_fmt(enum AVPixelFormat pixfmt);

/**
 * Loads props/mprops/driver_props
 */
int ff_vk_load_props(FFVulkanContext *s);

/**
 * Chooses a QF and loads it into a context.
 */
int ff_vk_qf_init(FFVulkanContext *s, FFVkQueueFamilyCtx *qf,
                  VkQueueFlagBits dev_family);

/**
 * Allocates/frees an execution pool.
 * ff_vk_exec_pool_init_desc() MUST be called if ff_vk_exec_descriptor_set_add()
 * has been called.
 */
int ff_vk_exec_pool_init(FFVulkanContext *s, FFVkQueueFamilyCtx *qf,
                         FFVkExecPool *pool, int nb_contexts,
                         int nb_queries, VkQueryType query_type, int query_64bit,
                         const void *query_create_pnext);
void ff_vk_exec_pool_free(FFVulkanContext *s, FFVkExecPool *pool);

/**
 * Retrieve an execution pool. Threadsafe.
 */
FFVkExecContext *ff_vk_exec_get(FFVkExecPool *pool);

/**
 * Performs nb_queries queries and returns their results and statuses.
 * Execution must have been waited on to produce valid results.
 */
VkResult ff_vk_exec_get_query(FFVulkanContext *s, FFVkExecContext *e,
                              void **data, int64_t *status);

/**
 * Start/submit/wait an execution.
 * ff_vk_exec_start() always waits on a submission, so using ff_vk_exec_wait()
 * is not necessary (unless using it is just better).
 */
int ff_vk_exec_start(FFVulkanContext *s, FFVkExecContext *e);
int ff_vk_exec_submit(FFVulkanContext *s, FFVkExecContext *e);
void ff_vk_exec_wait(FFVulkanContext *s, FFVkExecContext *e);

/**
 * Execution dependency management.
 * Can attach buffers to executions that will only be unref'd once the
 * buffer has finished executing.
 * Adding a frame dep will *lock the frame*, until either the dependencies
 * are discarded, the execution is submitted, or a failure happens.
 * update_frame will update the frame's properties before it is unlocked,
 * only if submission was successful.
 */
int ff_vk_exec_add_dep_buf(FFVulkanContext *s, FFVkExecContext *e,
                           AVBufferRef **deps, int nb_deps, int ref);
int ff_vk_exec_add_dep_frame(FFVulkanContext *s, FFVkExecContext *e, AVFrame *f,
                             VkPipelineStageFlagBits2 wait_stage,
                             VkPipelineStageFlagBits2 signal_stage);
void ff_vk_exec_update_frame(FFVulkanContext *s, FFVkExecContext *e, AVFrame *f,
                             VkImageMemoryBarrier2 *bar, uint32_t *nb_img_bar);
int ff_vk_exec_mirror_sem_value(FFVulkanContext *s, FFVkExecContext *e,
                                VkSemaphore *dst, uint64_t *dst_val,
                                AVFrame *f);
void ff_vk_exec_discard_deps(FFVulkanContext *s, FFVkExecContext *e);

/**
 * Create an imageview and add it as a dependency to an execution.
 */
int ff_vk_create_imageviews(FFVulkanContext *s, FFVkExecContext *e,
                            VkImageView views[AV_NUM_DATA_POINTERS],
                            AVFrame *f);

void ff_vk_frame_barrier(FFVulkanContext *s, FFVkExecContext *e,
                         AVFrame *pic, VkImageMemoryBarrier2 *bar, int *nb_bar,
                         VkPipelineStageFlags src_stage,
                         VkPipelineStageFlags dst_stage,
                         VkAccessFlagBits     new_access,
                         VkImageLayout        new_layout,
                         uint32_t             new_qf);

/**
 * Memory/buffer/image allocation helpers.
 */
int ff_vk_alloc_mem(FFVulkanContext *s, VkMemoryRequirements *req,
                    VkMemoryPropertyFlagBits req_flags, void *alloc_extension,
                    VkMemoryPropertyFlagBits *mem_flags, VkDeviceMemory *mem);
int ff_vk_create_buf(FFVulkanContext *s, FFVkBuffer *buf, size_t size,
                     void *pNext, void *alloc_pNext,
                     VkBufferUsageFlags usage, VkMemoryPropertyFlagBits flags);
int ff_vk_create_avbuf(FFVulkanContext *s, AVBufferRef **ref, size_t size,
                       void *pNext, void *alloc_pNext,
                       VkBufferUsageFlags usage, VkMemoryPropertyFlagBits flags);

/**
 * Buffer management code.
 */
int ff_vk_map_buffers(FFVulkanContext *s, FFVkBuffer **buf, uint8_t *mem[],
                      int nb_buffers, int invalidate);
int ff_vk_unmap_buffers(FFVulkanContext *s, FFVkBuffer **buf, int nb_buffers,
                        int flush);

static inline int ff_vk_map_buffer(FFVulkanContext *s, FFVkBuffer *buf, uint8_t **mem,
                                   int invalidate)
{
    return ff_vk_map_buffers(s, (FFVkBuffer *[]){ buf }, mem,
                             1, invalidate);
}

static inline int ff_vk_unmap_buffer(FFVulkanContext *s, FFVkBuffer *buf, int flush)
{
    return ff_vk_unmap_buffers(s, (FFVkBuffer *[]){ buf }, 1, flush);
}

void ff_vk_free_buf(FFVulkanContext *s, FFVkBuffer *buf);

/** Initialize a pool and create AVBufferRefs containing FFVkBuffer.
 * Threadsafe to use. Buffers are automatically mapped on creation if
 * VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT is set in mem_props. Users should
 * synchronize access themselvesd. Mainly meant for device-local buffers. */
int ff_vk_get_pooled_buffer(FFVulkanContext *ctx, AVBufferPool **buf_pool,
                            AVBufferRef **buf, VkBufferUsageFlags usage,
                            void *create_pNext, size_t size,
                            VkMemoryPropertyFlagBits mem_props);

/**
 * Create a sampler.
 */
int ff_vk_init_sampler(FFVulkanContext *s, VkSampler *sampler,
                       int unnorm_coords, VkFilter filt);

/**
 * Shader management.
 */
int ff_vk_shader_init(FFVulkanPipeline *pl, FFVkSPIRVShader *shd, const char *name,
                      VkShaderStageFlags stage, uint32_t required_subgroup_size);
void ff_vk_shader_set_compute_sizes(FFVkSPIRVShader *shd, int x, int y, int z);
void ff_vk_shader_print(void *ctx, FFVkSPIRVShader *shd, int prio);
int ff_vk_shader_create(FFVulkanContext *s, FFVkSPIRVShader *shd,
                        uint8_t *spirv, size_t spirv_size, const char *entrypoint);
void ff_vk_shader_free(FFVulkanContext *s, FFVkSPIRVShader *shd);

/**
 * Add/update push constants for execution.
 */
int ff_vk_add_push_constant(FFVulkanPipeline *pl, int offset, int size,
                            VkShaderStageFlagBits stage);
void ff_vk_update_push_exec(FFVulkanContext *s, FFVkExecContext *e,
                            FFVulkanPipeline *pl,
                            VkShaderStageFlagBits stage,
                            int offset, size_t size, void *src);

/**
 * Add descriptor to a pipeline. Must be called before pipeline init.
 */
int ff_vk_pipeline_descriptor_set_add(FFVulkanContext *s, FFVulkanPipeline *pl,
                                      FFVkSPIRVShader *shd,
                                      FFVulkanDescriptorSetBinding *desc, int nb,
                                      int read_only, int print_to_shader_only);

/* Initialize/free a pipeline. */
int ff_vk_init_compute_pipeline(FFVulkanContext *s, FFVulkanPipeline *pl,
                                FFVkSPIRVShader *shd);
void ff_vk_pipeline_free(FFVulkanContext *s, FFVulkanPipeline *pl);

/**
 * Register a pipeline with an exec pool.
 * Pool may be NULL if all descriptor sets are read-only.
 */
int ff_vk_exec_pipeline_register(FFVulkanContext *s, FFVkExecPool *pool,
                                 FFVulkanPipeline *pl);

/* Bind pipeline */
void ff_vk_exec_bind_pipeline(FFVulkanContext *s, FFVkExecContext *e,
                              FFVulkanPipeline *pl);

int ff_vk_set_descriptor_buffer(FFVulkanContext *s, FFVulkanPipeline *pl,
                                FFVkExecContext *e, int set, int bind, int offs,
                                VkDeviceAddress addr, VkDeviceSize len, VkFormat fmt);

void ff_vk_update_descriptor_img_array(FFVulkanContext *s, FFVulkanPipeline *pl,
                                       FFVkExecContext *e, AVFrame *f,
                                       VkImageView *views, int set, int binding,
                                       VkImageLayout layout, VkSampler sampler);

/**
 * Frees main context.
 */
void ff_vk_uninit(FFVulkanContext *s);

#endif /* AVUTIL_VULKAN_H */
