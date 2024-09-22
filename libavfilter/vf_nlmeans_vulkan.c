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

#include "libavutil/mem.h"
#include "libavutil/random_seed.h"
#include "libavutil/opt.h"
#include "vulkan_filter.h"
#include "vulkan_spirv.h"

#include "filters.h"
#include "video.h"

#define TYPE_NAME  "vec4"
#define TYPE_ELEMS 4
#define TYPE_SIZE  (TYPE_ELEMS*4)

typedef struct NLMeansVulkanContext {
    FFVulkanContext vkctx;

    int initialized;
    FFVkExecPool e;
    FFVkQueueFamilyCtx qf;
    VkSampler sampler;

    AVBufferPool *integral_buf_pool;
    AVBufferPool *ws_buf_pool;

    FFVkBuffer xyoffsets_buf;

    int pl_weights_rows;
    FFVulkanPipeline pl_weights;
    FFVkSPIRVShader shd_weights;

    FFVulkanPipeline pl_denoise;
    FFVkSPIRVShader shd_denoise;

    int *xoffsets;
    int *yoffsets;
    int nb_offsets;
    float strength[4];
    int patch[4];

    struct nlmeans_opts {
        int r;
        double s;
        double sc[4];
        int p;
        int pc[4];
        int t;
    } opts;
} NLMeansVulkanContext;

extern const char *ff_source_prefix_sum_comp;

static void insert_first(FFVkSPIRVShader *shd, int r, const char *off, int horiz, int plane, int comp)
{
    GLSLF(4, s1    = texture(input_img[%i], pos + ivec2(%i + %s, %i + %s))[%i];
          ,plane, horiz ? r : 0, horiz ? off : "0", !horiz ? r : 0, !horiz ? off : "0", comp);

    GLSLF(4, s2[0] = texture(input_img[%i], pos + offs[0] + ivec2(%i + %s, %i + %s))[%i];
          ,plane, horiz ? r : 0, horiz ? off : "0", !horiz ? r : 0, !horiz ? off : "0", comp);
    GLSLF(4, s2[1] = texture(input_img[%i], pos + offs[1] + ivec2(%i + %s, %i + %s))[%i];
          ,plane, horiz ? r : 0, horiz ? off : "0", !horiz ? r : 0, !horiz ? off : "0", comp);
    GLSLF(4, s2[2] = texture(input_img[%i], pos + offs[2] + ivec2(%i + %s, %i + %s))[%i];
          ,plane, horiz ? r : 0, horiz ? off : "0", !horiz ? r : 0, !horiz ? off : "0", comp);
    GLSLF(4, s2[3] = texture(input_img[%i], pos + offs[3] + ivec2(%i + %s, %i + %s))[%i];
          ,plane, horiz ? r : 0, horiz ? off : "0", !horiz ? r : 0, !horiz ? off : "0", comp);

    GLSLC(4, s2 = (s1 - s2) * (s1 - s2);                                                    );
}

static void insert_horizontal_pass(FFVkSPIRVShader *shd, int nb_rows, int first, int plane, int comp)
{
    GLSLF(1, pos.y = int(gl_GlobalInvocationID.x) * %i;                           ,nb_rows);
    if (!first)
        GLSLC(1, barrier();                                                       );
    GLSLC(0,                                                                      );
    GLSLF(1, if (pos.y < height[%i]) {                                            ,plane);
    GLSLC(2,     #pragma unroll(1)                                                );
    GLSLF(2,     for (r = 0; r < %i; r++) {                                       ,nb_rows);
    GLSLC(3,         prefix_sum = DTYPE(0);                                       );
    GLSLC(3,         offset = int_stride * uint64_t(pos.y + r);                   );
    GLSLC(3,         dst = DataBuffer(uint64_t(integral_data) + offset);          );
    GLSLC(0,                                                                      );
    GLSLF(3,         for (pos.x = 0; pos.x < width[%i]; pos.x++) {                ,plane);
    if (first)
        insert_first(shd, 0, "r", 0, plane, comp);
    else
        GLSLC(4,         s2 = dst.v[pos.x];                                       );
    GLSLC(4,             dst.v[pos.x] = s2 + prefix_sum;                          );
    GLSLC(4,             prefix_sum += s2;                                        );
    GLSLC(3,         }                                                            );
    GLSLC(2,     }                                                                );
    GLSLC(1, }                                                                    );
    GLSLC(0,                                                                      );
}

static void insert_vertical_pass(FFVkSPIRVShader *shd, int nb_rows, int first, int plane, int comp)
{
    GLSLF(1, pos.x = int(gl_GlobalInvocationID.x) * %i;                           ,nb_rows);
    GLSLC(1, #pragma unroll(1)                                                    );
    GLSLF(1, for (r = 0; r < %i; r++)                                             ,nb_rows);
    GLSLC(2,     psum[r] = DTYPE(0);                                              );
    GLSLC(0,                                                                      );
    if (!first)
        GLSLC(1, barrier();                                                       );
    GLSLC(0,                                                                      );
    GLSLF(1, if (pos.x < width[%i]) {                                             ,plane);
    GLSLF(2,     for (pos.y = 0; pos.y < height[%i]; pos.y++) {                   ,plane);
    GLSLC(3,         offset = int_stride * uint64_t(pos.y);                       );
    GLSLC(3,         dst = DataBuffer(uint64_t(integral_data) + offset);          );
    GLSLC(0,                                                                      );
    GLSLC(3,         #pragma unroll(1)                                            );
    GLSLF(3,         for (r = 0; r < %i; r++) {                                   ,nb_rows);
    if (first)
        insert_first(shd, 0, "r", 1, plane, comp);
    else
        GLSLC(4,         s2 = dst.v[pos.x + r];                                   );
    GLSLC(4,             dst.v[pos.x + r] = s2 + psum[r];                         );
    GLSLC(4,             psum[r] += s2;                                           );
    GLSLC(3,         }                                                            );
    GLSLC(2,     }                                                                );
    GLSLC(1, }                                                                    );
    GLSLC(0,                                                                      );
}

static void insert_weights_pass(FFVkSPIRVShader *shd, int nb_rows, int vert,
                                int t, int dst_comp, int plane, int comp)
{
    GLSLF(1, p = patch_size[%i];                                              ,dst_comp);
    GLSLC(0,                                                                  );
    GLSLC(1, barrier();                                                       );
    GLSLC(0,                                                                  );
    if (!vert) {
        GLSLF(1, for (pos.y = 0; pos.y < height[%i]; pos.y++) {               ,plane);
        GLSLF(2,     if (gl_GlobalInvocationID.x*%i >= width[%i])             ,nb_rows, plane);
        GLSLC(3,         break;                                               );
        GLSLF(2,     for (r = 0; r < %i; r++) {                               ,nb_rows);
        GLSLF(3,         pos.x = int(gl_GlobalInvocationID.x) * %i + r;       ,nb_rows);
    } else {
        GLSLF(1, for (pos.x = 0; pos.x < width[%i]; pos.x++) {                ,plane);
        GLSLF(2,     if (gl_GlobalInvocationID.x*%i >= height[%i])            ,nb_rows, plane);
        GLSLC(3,         break;                                               );
        GLSLF(2,     for (r = 0; r < %i; r++) {                               ,nb_rows);
        GLSLF(3,         pos.y = int(gl_GlobalInvocationID.x) * %i + r;       ,nb_rows);
    }
    GLSLC(0,                                                                  );
    GLSLC(3,         a = DTYPE(0);                                            );
    GLSLC(3,         b = DTYPE(0);                                            );
    GLSLC(3,         c = DTYPE(0);                                            );
    GLSLC(3,         d = DTYPE(0);                                            );
    GLSLC(0,                                                                  );
    GLSLC(3,         lt = ((pos.x - p) < 0) || ((pos.y - p) < 0);             );
    GLSLC(0,                                                                  );
    GLSLF(3,         src[0] = texture(input_img[%i], pos + offs[0])[%i];      ,plane, comp);
    GLSLF(3,         src[1] = texture(input_img[%i], pos + offs[1])[%i];      ,plane, comp);
    GLSLF(3,         src[2] = texture(input_img[%i], pos + offs[2])[%i];      ,plane, comp);
    GLSLF(3,         src[3] = texture(input_img[%i], pos + offs[3])[%i];      ,plane, comp);
    GLSLC(0,                                                                  );
    GLSLC(3,         if (lt == false) {                                       );
    GLSLC(3,             offset = int_stride * uint64_t(pos.y - p);           );
    GLSLC(3,             dst = DataBuffer(uint64_t(integral_data) + offset);  );
    GLSLC(4,             a = dst.v[pos.x - p];                                );
    GLSLC(4,             c = dst.v[pos.x + p];                                );
    GLSLC(3,             offset = int_stride * uint64_t(pos.y + p);           );
    GLSLC(3,             dst = DataBuffer(uint64_t(integral_data) + offset);  );
    GLSLC(4,             b = dst.v[pos.x - p];                                );
    GLSLC(4,             d = dst.v[pos.x + p];                                );
    GLSLC(3,         }                                                        );
    GLSLC(0,                                                                  );
    GLSLC(3,         patch_diff = d + a - b - c;                              );
    GLSLF(3,         w = exp(patch_diff * strength[%i]);                      ,dst_comp);
    GLSLC(3,         w_sum = w[0] + w[1] + w[2] + w[3];                       );
    GLSLC(3,         sum = dot(w, src*255);                                   );
    GLSLC(0,                                                                  );
    if (t > 1) {
        GLSLF(3,         atomicAdd(weights_%i[pos.y*ws_stride[%i] + pos.x], w_sum);   ,dst_comp, dst_comp);
        GLSLF(3,         atomicAdd(sums_%i[pos.y*ws_stride[%i] + pos.x], sum);        ,dst_comp, dst_comp);
    } else {
        GLSLF(3,         weights_%i[pos.y*ws_stride[%i] + pos.x] += w_sum;            ,dst_comp, dst_comp);
        GLSLF(3,         sums_%i[pos.y*ws_stride[%i] + pos.x] += sum;                 ,dst_comp, dst_comp);
    }
    GLSLC(2,     }                                                            );
    GLSLC(1, }                                                                );
}

typedef struct HorizontalPushData {
    uint32_t width[4];
    uint32_t height[4];
    uint32_t ws_stride[4];
    int32_t  patch_size[4];
    float    strength[4];
    VkDeviceAddress integral_base;
    uint64_t integral_size;
    uint64_t int_stride;
    uint32_t xyoffs_start;
} HorizontalPushData;

static av_cold int init_weights_pipeline(FFVulkanContext *vkctx, FFVkExecPool *exec,
                                         FFVulkanPipeline *pl, FFVkSPIRVShader *shd,
                                         VkSampler sampler, FFVkSPIRVCompiler *spv,
                                         int width, int height, int t,
                                         const AVPixFmtDescriptor *desc,
                                         int planes, int *nb_rows)
{
    int err;
    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    FFVulkanDescriptorSetBinding *desc_set;
    int max_dim = FFMAX(width, height);
    uint32_t max_wg = vkctx->props.properties.limits.maxComputeWorkGroupSize[0];
    int wg_size, wg_rows;

    /* Round the max workgroup size to the previous power of two */
    wg_size = max_wg;
    wg_rows = 1;

    if (max_wg > max_dim) {
        wg_size = max_dim;
    } else if (max_wg < max_dim) {
        /* Make it fit */
        while (wg_size*wg_rows < max_dim)
            wg_rows++;
    }

    RET(ff_vk_shader_init(pl, shd, "nlmeans_weights", VK_SHADER_STAGE_COMPUTE_BIT, 0));
    ff_vk_shader_set_compute_sizes(shd, wg_size, 1, 1);
    *nb_rows = wg_rows;

    if (t > 1)
        GLSLC(0, #extension GL_EXT_shader_atomic_float : require              );
    GLSLC(0, #extension GL_ARB_gpu_shader_int64 : require                     );
    GLSLC(0,                                                                  );
    GLSLF(0, #define DTYPE %s                                                 ,TYPE_NAME);
    GLSLF(0, #define T_ALIGN %i                                               ,TYPE_SIZE);
    GLSLC(0,                                                                  );
    GLSLC(0, layout(buffer_reference, buffer_reference_align = T_ALIGN) buffer DataBuffer {  );
    GLSLC(1,     DTYPE v[];                                                   );
    GLSLC(0, };                                                               );
    GLSLC(0,                                                                  );
    GLSLC(0, layout(push_constant, std430) uniform pushConstants {            );
    GLSLC(1,     uvec4 width;                                                 );
    GLSLC(1,     uvec4 height;                                                );
    GLSLC(1,     uvec4 ws_stride;                                             );
    GLSLC(1,     ivec4 patch_size;                                            );
    GLSLC(1,     vec4 strength;                                               );
    GLSLC(1,     DataBuffer integral_base;                                    );
    GLSLC(1,     uint64_t integral_size;                                      );
    GLSLC(1,     uint64_t int_stride;                                         );
    GLSLC(1,     uint xyoffs_start;                                           );
    GLSLC(0, };                                                               );
    GLSLC(0,                                                                  );

    ff_vk_add_push_constant(pl, 0, sizeof(HorizontalPushData), VK_SHADER_STAGE_COMPUTE_BIT);

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name       = "input_img",
            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .samplers   = DUP_SAMPLER(sampler),
        },
        {
            .name        = "weights_buffer_0",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float weights_0[];",
        },
        {
            .name        = "sums_buffer_0",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float sums_0[];",
        },
        {
            .name        = "weights_buffer_1",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float weights_1[];",
        },
        {
            .name        = "sums_buffer_1",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float sums_1[];",
        },
        {
            .name        = "weights_buffer_2",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float weights_2[];",
        },
        {
            .name        = "sums_buffer_2",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float sums_2[];",
        },
        {
            .name        = "weights_buffer_3",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float weights_3[];",
        },
        {
            .name        = "sums_buffer_3",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float sums_3[];",
        },
    };
    RET(ff_vk_pipeline_descriptor_set_add(vkctx, pl, shd, desc_set, 1 + 2*desc->nb_components, 0, 0));

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "xyoffsets_buffer",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .mem_quali   = "readonly",
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "ivec2 xyoffsets[];",
        },
    };
    RET(ff_vk_pipeline_descriptor_set_add(vkctx, pl, shd, desc_set, 1, 1, 0));

    GLSLC(0,                                                                     );
    GLSLC(0, void main()                                                         );
    GLSLC(0, {                                                                   );
    GLSLC(1,     uint64_t offset;                                                );
    GLSLC(1,     DataBuffer dst;                                                 );
    GLSLC(1,     float s1;                                                       );
    GLSLC(1,     DTYPE s2;                                                       );
    GLSLC(1,     DTYPE prefix_sum;                                               );
    GLSLF(1,     DTYPE psum[%i];                                                 ,*nb_rows);
    GLSLC(1,     int r;                                                          );
    GLSLC(1,     ivec2 pos;                                                      );
    GLSLC(1,     int p;                                                          );
    GLSLC(0,                                                                     );
    GLSLC(1,     DataBuffer integral_data;                                       );
    GLSLF(1,     ivec2 offs[%i];                                                 ,TYPE_ELEMS);
    GLSLC(0,                                                                     );
    GLSLC(1,     int invoc_idx = int(gl_WorkGroupID.z);                          );
    GLSLC(0,                                                                     );
    GLSLC(1,     offset = integral_size * invoc_idx;                             );
    GLSLC(1,     integral_data = DataBuffer(uint64_t(integral_base) + offset);   );
    for (int i = 0; i < TYPE_ELEMS; i++)
        GLSLF(1, offs[%i] = xyoffsets[xyoffs_start + %i*invoc_idx + %i];         ,i,TYPE_ELEMS,i);
    GLSLC(0,                                                                     );
    GLSLC(1,     DTYPE a;                                                        );
    GLSLC(1,     DTYPE b;                                                        );
    GLSLC(1,     DTYPE c;                                                        );
    GLSLC(1,     DTYPE d;                                                        );
    GLSLC(0,                                                                     );
    GLSLC(1,     DTYPE patch_diff;                                               );
    if (TYPE_ELEMS == 4) {
        GLSLC(1, vec4 src;                                                       );
        GLSLC(1, vec4 w;                                                         );
    } else {
        GLSLC(1, vec4 src[4];                                                    );
        GLSLC(1, vec4 w[4];                                                      );
    }
    GLSLC(1,     float w_sum;                                                    );
    GLSLC(1,     float sum;                                                      );
    GLSLC(0,                                                                     );
    GLSLC(1,     bool lt;                                                        );
    GLSLC(1,     bool gt;                                                        );
    GLSLC(0,                                                                     );

    for (int i = 0; i < desc->nb_components; i++) {
        int off = desc->comp[i].offset / (FFALIGN(desc->comp[i].depth, 8)/8);
        if (width >= height) {
            insert_horizontal_pass(shd, *nb_rows, 1, desc->comp[i].plane, off);
            insert_vertical_pass(shd, *nb_rows, 0, desc->comp[i].plane, off);
            insert_weights_pass(shd, *nb_rows, 0, t, i, desc->comp[i].plane, off);
        } else {
            insert_vertical_pass(shd, *nb_rows, 1, desc->comp[i].plane, off);
            insert_horizontal_pass(shd, *nb_rows, 0, desc->comp[i].plane, off);
            insert_weights_pass(shd, *nb_rows, 1, t, i, desc->comp[i].plane, off);
        }
    }

    GLSLC(0, }                                                                   );

    RET(spv->compile_shader(spv, vkctx, shd, &spv_data, &spv_len, "main", &spv_opaque));
    RET(ff_vk_shader_create(vkctx, shd, spv_data, spv_len, "main"));

    RET(ff_vk_init_compute_pipeline(vkctx, pl, shd));
    RET(ff_vk_exec_pipeline_register(vkctx, exec, pl));

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);

    return err;
}

typedef struct DenoisePushData {
    uint32_t ws_stride[4];
} DenoisePushData;

static av_cold int init_denoise_pipeline(FFVulkanContext *vkctx, FFVkExecPool *exec,
                                         FFVulkanPipeline *pl, FFVkSPIRVShader *shd,
                                         VkSampler sampler, FFVkSPIRVCompiler *spv,
                                         const AVPixFmtDescriptor *desc, int planes)
{
    int err;
    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    FFVulkanDescriptorSetBinding *desc_set;

    RET(ff_vk_shader_init(pl, shd, "nlmeans_denoise",
                          VK_SHADER_STAGE_COMPUTE_BIT, 0));

    ff_vk_shader_set_compute_sizes(shd, 32, 32, 1);

    GLSLC(0, layout(push_constant, std430) uniform pushConstants {        );
    GLSLC(1,    uvec4 ws_stride;                                          );
    GLSLC(0, };                                                           );

    ff_vk_add_push_constant(pl, 0, sizeof(DenoisePushData), VK_SHADER_STAGE_COMPUTE_BIT);

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "input_img",
            .type        = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .dimensions  = 2,
            .elems       = planes,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .samplers    = DUP_SAMPLER(sampler),
        },
        {
            .name        = "output_img",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .mem_layout  = ff_vk_shader_rep_fmt(vkctx->output_format),
            .mem_quali   = "writeonly",
            .dimensions  = 2,
            .elems       = planes,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    RET(ff_vk_pipeline_descriptor_set_add(vkctx, pl, shd, desc_set, 2, 0, 0));

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "weights_buffer_0",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .mem_quali   = "readonly",
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float weights_0[];",
        },
        {
            .name        = "sums_buffer_0",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .mem_quali   = "readonly",
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float sums_0[];",
        },
        {
            .name        = "weights_buffer_1",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .mem_quali   = "readonly",
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float weights_1[];",
        },
        {
            .name        = "sums_buffer_1",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .mem_quali   = "readonly",
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float sums_1[];",
        },
        {
            .name        = "weights_buffer_2",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .mem_quali   = "readonly",
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float weights_2[];",
        },
        {
            .name        = "sums_buffer_2",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .mem_quali   = "readonly",
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float sums_2[];",
        },
        {
            .name        = "weights_buffer_3",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .mem_quali   = "readonly",
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float weights_3[];",
        },
        {
            .name        = "sums_buffer_3",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .mem_quali   = "readonly",
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float sums_3[];",
        },
    };

    RET(ff_vk_pipeline_descriptor_set_add(vkctx, pl, shd, desc_set, 2*desc->nb_components, 0, 0));

    GLSLC(0, void main()                                                      );
    GLSLC(0, {                                                                );
    GLSLC(1,     ivec2 size;                                                  );
    GLSLC(1,     const ivec2 pos = ivec2(gl_GlobalInvocationID.xy);           );
    GLSLC(1,     const uint plane = uint(gl_WorkGroupID.z);                   );
    GLSLC(0,                                                                  );
    GLSLC(1,     float w_sum;                                                 );
    GLSLC(1,     float sum;                                                   );
    GLSLC(1,     vec4 src;                                                    );
    GLSLC(1,     vec4 r;                                                      );
    GLSLC(0,                                                                  );
    GLSLC(1,     size = imageSize(output_img[plane]);                         );
    GLSLC(1,     if (!IS_WITHIN(pos, size))                                   );
    GLSLC(2,         return;                                                  );
    GLSLC(0,                                                                  );
    GLSLC(1,     src = texture(input_img[plane], pos);                        );
    GLSLC(0,                                                                  );
    for (int c = 0; c < desc->nb_components; c++) {
        int off = desc->comp[c].offset / (FFALIGN(desc->comp[c].depth, 8)/8);
        GLSLF(1, if (plane == %i) {                                              ,desc->comp[c].plane);
        GLSLF(2,     w_sum = weights_%i[pos.y*ws_stride[%i] + pos.x];                           ,c, c);
        GLSLF(2,     sum = sums_%i[pos.y*ws_stride[%i] + pos.x];                                ,c, c);
        GLSLF(2,     r[%i] = (sum + src[%i]*255) / (1.0 + w_sum) / 255;                     ,off, off);
        GLSLC(1, }                                                                                   );
        GLSLC(0,                                                                                     );
    }
    GLSLC(1, imageStore(output_img[plane], pos, r);                           );
    GLSLC(0, }                                                                );

    RET(spv->compile_shader(spv, vkctx, shd, &spv_data, &spv_len, "main", &spv_opaque));
    RET(ff_vk_shader_create(vkctx, shd, spv_data, spv_len, "main"));

    RET(ff_vk_init_compute_pipeline(vkctx, pl, shd));
    RET(ff_vk_exec_pipeline_register(vkctx, exec, pl));

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);

    return err;
}

static av_cold int init_filter(AVFilterContext *ctx)
{
    int rad, err;
    int xcnt = 0, ycnt = 0;
    NLMeansVulkanContext *s = ctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);
    FFVkSPIRVCompiler *spv = NULL;
    int *offsets_buf;
    int offsets_dispatched = 0, nb_dispatches = 0;

    const AVPixFmtDescriptor *desc;
    desc = av_pix_fmt_desc_get(vkctx->output_format);
    if (!desc)
        return AVERROR(EINVAL);

    if (!(s->opts.r & 1)) {
        s->opts.r |= 1;
        av_log(ctx, AV_LOG_WARNING, "Research size should be odd, setting to %i",
               s->opts.r);
    }

    if (!(s->opts.p & 1)) {
        s->opts.p |= 1;
        av_log(ctx, AV_LOG_WARNING, "Patch size should be odd, setting to %i",
               s->opts.p);
    }

    for (int i = 0; i < 4; i++) {
        double str = (s->opts.sc[i] > 1.0) ? s->opts.sc[i] : s->opts.s;
        int ps = (s->opts.pc[i] ? s->opts.pc[i] : s->opts.p);
        str  = 10.0f*str;
        str *= -str;
        str  = 255.0*255.0 / str;
        s->strength[i] = str;
        if (!(ps & 1)) {
            ps |= 1;
            av_log(ctx, AV_LOG_WARNING, "Patch size should be odd, setting to %i",
                   ps);
        }
        s->patch[i] = ps / 2;
    }

    rad = s->opts.r/2;
    s->nb_offsets = (2*rad + 1)*(2*rad + 1) - 1;
    s->xoffsets = av_malloc(s->nb_offsets*sizeof(*s->xoffsets));
    s->yoffsets = av_malloc(s->nb_offsets*sizeof(*s->yoffsets));
    s->nb_offsets = 0;

    for (int x = -rad; x <= rad; x++) {
        for (int y = -rad; y <= rad; y++) {
            if (!x && !y)
                continue;

            s->xoffsets[xcnt++] = x;
            s->yoffsets[ycnt++] = y;
            s->nb_offsets++;
        }
    }

    RET(ff_vk_create_buf(&s->vkctx, &s->xyoffsets_buf, 2*s->nb_offsets*sizeof(int32_t), NULL, NULL,
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
    RET(ff_vk_map_buffer(&s->vkctx, &s->xyoffsets_buf, (uint8_t **)&offsets_buf, 0));

    for (int i = 0; i < 2*s->nb_offsets; i += 2) {
        offsets_buf[i + 0] = s->xoffsets[i >> 1];
        offsets_buf[i + 1] = s->yoffsets[i >> 1];
    }

    RET(ff_vk_unmap_buffer(&s->vkctx, &s->xyoffsets_buf, 1));

    s->opts.t = FFMIN(s->opts.t, (FFALIGN(s->nb_offsets, TYPE_ELEMS) / TYPE_ELEMS));
    if (!vkctx->atomic_float_feats.shaderBufferFloat32AtomicAdd) {
        av_log(ctx, AV_LOG_WARNING, "Device doesn't support atomic float adds, "
               "disabling dispatch parallelism\n");
        s->opts.t = 1;
    }

    spv = ff_vk_spirv_init();
    if (!spv) {
        av_log(ctx, AV_LOG_ERROR, "Unable to initialize SPIR-V compiler!\n");
        return AVERROR_EXTERNAL;
    }

    ff_vk_qf_init(vkctx, &s->qf, VK_QUEUE_COMPUTE_BIT);
    RET(ff_vk_exec_pool_init(vkctx, &s->qf, &s->e, 1, 0, 0, 0, NULL));
    RET(ff_vk_init_sampler(vkctx, &s->sampler, 1, VK_FILTER_NEAREST));

    RET(init_weights_pipeline(vkctx, &s->e, &s->pl_weights, &s->shd_weights, s->sampler,
                              spv, s->vkctx.output_width, s->vkctx.output_height,
                              s->opts.t, desc, planes, &s->pl_weights_rows));

    RET(init_denoise_pipeline(vkctx, &s->e, &s->pl_denoise, &s->shd_denoise, s->sampler,
                              spv, desc, planes));

    RET(ff_vk_set_descriptor_buffer(&s->vkctx, &s->pl_weights, NULL, 1, 0, 0,
                                    &s->xyoffsets_buf, 0, s->xyoffsets_buf.size,
                                    VK_FORMAT_UNDEFINED));

    do {
        int wg_invoc = FFMIN((s->nb_offsets - offsets_dispatched)/TYPE_ELEMS, s->opts.t);
        wg_invoc = FFMIN(wg_invoc, vkctx->props.properties.limits.maxComputeWorkGroupCount[2]);
        offsets_dispatched += wg_invoc * TYPE_ELEMS;
        nb_dispatches++;
    } while (offsets_dispatched < s->nb_offsets);

    av_log(ctx, AV_LOG_VERBOSE, "Filter initialized, %i x/y offsets, %i dispatches\n",
           s->nb_offsets, nb_dispatches);

    s->initialized = 1;

fail:
    if (spv)
        spv->uninit(&spv);

    return err;
}

static int denoise_pass(NLMeansVulkanContext *s, FFVkExecContext *exec,
                        FFVkBuffer *ws_vk, uint32_t ws_stride[4])
{
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;
    VkBufferMemoryBarrier2 buf_bar[8];
    int nb_buf_bar = 0;

    DenoisePushData pd = {
        { ws_stride[0], ws_stride[1], ws_stride[2], ws_stride[3] },
    };

    /* Denoise pass pipeline */
    ff_vk_exec_bind_pipeline(vkctx, exec, &s->pl_denoise);

    /* Push data */
    ff_vk_update_push_exec(vkctx, exec, &s->pl_denoise, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(pd), &pd);

    buf_bar[nb_buf_bar++] = (VkBufferMemoryBarrier2) {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = ws_vk->stage,
        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = ws_vk->access,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = ws_vk->buf,
        .size = ws_vk->size,
        .offset = 0,
    };

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pBufferMemoryBarriers = buf_bar,
            .bufferMemoryBarrierCount = nb_buf_bar,
        });
    ws_vk->stage = buf_bar[0].dstStageMask;
    ws_vk->access = buf_bar[0].dstAccessMask;

    /* End of denoise pass */
    vk->CmdDispatch(exec->buf,
                    FFALIGN(vkctx->output_width,  s->pl_denoise.wg_size[0])/s->pl_denoise.wg_size[0],
                    FFALIGN(vkctx->output_height, s->pl_denoise.wg_size[1])/s->pl_denoise.wg_size[1],
                    av_pix_fmt_count_planes(s->vkctx.output_format));

    return 0;
}

static int nlmeans_vulkan_filter_frame(AVFilterLink *link, AVFrame *in)
{
    int err;
    AVFrame *out = NULL;
    AVFilterContext *ctx = link->dst;
    NLMeansVulkanContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;

    const AVPixFmtDescriptor *desc;
    int plane_widths[4];
    int plane_heights[4];

    int offsets_dispatched = 0;

    /* Integral */
    AVBufferRef *integral_buf = NULL;
    FFVkBuffer *integral_vk;
    size_t int_stride;
    size_t int_size;

    /* Weights/sums */
    AVBufferRef *ws_buf = NULL;
    FFVkBuffer *ws_vk;
    VkDeviceSize weights_offs[4];
    VkDeviceSize sums_offs[4];
    uint32_t ws_stride[4];
    size_t ws_size[4];
    size_t ws_total_size = 0;

    FFVkExecContext *exec;
    VkImageView in_views[AV_NUM_DATA_POINTERS];
    VkImageView out_views[AV_NUM_DATA_POINTERS];
    VkImageMemoryBarrier2 img_bar[8];
    int nb_img_bar = 0;
    VkBufferMemoryBarrier2 buf_bar[8];
    int nb_buf_bar = 0;

    if (!s->initialized)
        RET(init_filter(ctx));

    desc = av_pix_fmt_desc_get(vkctx->output_format);
    if (!desc)
        return AVERROR(EINVAL);

    /* Integral image */
    int_stride = s->pl_weights.wg_size[0]*s->pl_weights_rows*TYPE_SIZE;
    int_size = s->pl_weights.wg_size[0]*s->pl_weights_rows*int_stride;

    /* Plane dimensions */
    for (int i = 0; i < desc->nb_components; i++) {
        plane_widths[i] = !i || (i == 3) ? vkctx->output_width : AV_CEIL_RSHIFT(vkctx->output_width, desc->log2_chroma_w);
        plane_heights[i] = !i || (i == 3) ? vkctx->output_height : AV_CEIL_RSHIFT(vkctx->output_height, desc->log2_chroma_w);
        plane_widths[i]  = FFALIGN(plane_widths[i],  s->pl_denoise.wg_size[0]);
        plane_heights[i] = FFALIGN(plane_heights[i], s->pl_denoise.wg_size[1]);

        ws_stride[i] = plane_widths[i];
        ws_size[i] = ws_stride[i] * plane_heights[i] * sizeof(float);
        ws_total_size += ws_size[i];
    }

    /* Buffers */
    err = ff_vk_get_pooled_buffer(&s->vkctx, &s->integral_buf_pool, &integral_buf,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                  NULL,
                                  s->opts.t * int_size,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (err < 0)
        return err;
    integral_vk = (FFVkBuffer *)integral_buf->data;

    err = ff_vk_get_pooled_buffer(&s->vkctx, &s->ws_buf_pool, &ws_buf,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                  NULL,
                                  ws_total_size * 2,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (err < 0)
        return err;
    ws_vk = (FFVkBuffer *)ws_buf->data;

    weights_offs[0] = 0;
    sums_offs[0] = ws_total_size;
    for (int i = 1; i < desc->nb_components; i++) {
        weights_offs[i] = weights_offs[i - 1] + ws_size[i - 1];
        sums_offs[i] = sums_offs[i - 1] + ws_size[i - 1];
    }

    /* Output frame */
    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    /* Execution context */
    exec = ff_vk_exec_get(&s->e);
    ff_vk_exec_start(vkctx, exec);

    /* Dependencies */
    RET(ff_vk_exec_add_dep_frame(vkctx, exec, in,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
    RET(ff_vk_exec_add_dep_frame(vkctx, exec, out,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));

    RET(ff_vk_exec_add_dep_buf(vkctx, exec, &integral_buf, 1, 0));
    integral_buf = NULL;

    RET(ff_vk_exec_add_dep_buf(vkctx, exec, &ws_buf,       1, 0));
    ws_buf = NULL;

    /* Input frame prep */
    RET(ff_vk_create_imageviews(vkctx, exec, in_views, in));
    ff_vk_frame_barrier(vkctx, exec, in, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_QUEUE_FAMILY_IGNORED);

    /* Output frame prep */
    RET(ff_vk_create_imageviews(vkctx, exec, out_views, out));
    ff_vk_frame_barrier(vkctx, exec, out, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);

    nb_buf_bar = 0;
    buf_bar[nb_buf_bar++] = (VkBufferMemoryBarrier2) {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = ws_vk->stage,
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = ws_vk->access,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = ws_vk->buf,
        .size = ws_vk->size,
        .offset = 0,
    };
    buf_bar[nb_buf_bar++] = (VkBufferMemoryBarrier2) {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = integral_vk->stage,
        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = integral_vk->access,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                         VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = integral_vk->buf,
        .size = integral_vk->size,
        .offset = 0,
    };

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pImageMemoryBarriers = img_bar,
            .imageMemoryBarrierCount = nb_img_bar,
            .pBufferMemoryBarriers = buf_bar,
            .bufferMemoryBarrierCount = nb_buf_bar,
        });
    ws_vk->stage = buf_bar[0].dstStageMask;
    ws_vk->access = buf_bar[0].dstAccessMask;
    integral_vk->stage = buf_bar[1].dstStageMask;
    integral_vk->access = buf_bar[1].dstAccessMask;

    /* Buffer zeroing */
    vk->CmdFillBuffer(exec->buf, ws_vk->buf, 0, ws_vk->size, 0x0);

    nb_buf_bar = 0;
    buf_bar[nb_buf_bar++] = (VkBufferMemoryBarrier2) {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = ws_vk->stage,
        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = ws_vk->access,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                         VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = ws_vk->buf,
        .size = ws_vk->size,
        .offset = 0,
    };

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pBufferMemoryBarriers = buf_bar,
            .bufferMemoryBarrierCount = nb_buf_bar,
        });
    ws_vk->stage = buf_bar[0].dstStageMask;
    ws_vk->access = buf_bar[0].dstAccessMask;

    /* Update weights descriptors */
    ff_vk_update_descriptor_img_array(vkctx, &s->pl_weights, exec, in, in_views, 0, 0,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      s->sampler);
    for (int i = 0; i < desc->nb_components; i++) {
        RET(ff_vk_set_descriptor_buffer(&s->vkctx, &s->pl_weights, exec, 0, 1 + i*2 + 0, 0,
                                        ws_vk, weights_offs[i], ws_size[i],
                                        VK_FORMAT_UNDEFINED));
        RET(ff_vk_set_descriptor_buffer(&s->vkctx, &s->pl_weights, exec, 0, 1 + i*2 + 1, 0,
                                        ws_vk, sums_offs[i], ws_size[i],
                                        VK_FORMAT_UNDEFINED));
    }

    /* Update denoise descriptors */
    ff_vk_update_descriptor_img_array(vkctx, &s->pl_denoise, exec, in, in_views, 0, 0,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      s->sampler);
    ff_vk_update_descriptor_img_array(vkctx, &s->pl_denoise, exec, out, out_views, 0, 1,
                                      VK_IMAGE_LAYOUT_GENERAL, s->sampler);
    for (int i = 0; i < desc->nb_components; i++) {
        RET(ff_vk_set_descriptor_buffer(&s->vkctx, &s->pl_denoise, exec, 1, i*2 + 0, 0,
                                        ws_vk, weights_offs[i], ws_size[i],
                                        VK_FORMAT_UNDEFINED));
        RET(ff_vk_set_descriptor_buffer(&s->vkctx, &s->pl_denoise, exec, 1, i*2 + 1, 0,
                                        ws_vk, sums_offs[i], ws_size[i],
                                        VK_FORMAT_UNDEFINED));
    }

    /* Weights pipeline */
    ff_vk_exec_bind_pipeline(vkctx, exec, &s->pl_weights);

    do {
        int wg_invoc;
        HorizontalPushData pd = {
            { plane_widths[0], plane_widths[1], plane_widths[2], plane_widths[3] },
            { plane_heights[0], plane_heights[1], plane_heights[2], plane_heights[3] },
            { ws_stride[0], ws_stride[1], ws_stride[2], ws_stride[3] },
            { s->patch[0], s->patch[1], s->patch[2], s->patch[3] },
            { s->strength[0], s->strength[1], s->strength[2], s->strength[2], },
            integral_vk->address,
            (uint64_t)int_size,
            (uint64_t)int_stride,
            offsets_dispatched,
        };

        /* Push data */
        ff_vk_update_push_exec(vkctx, exec, &s->pl_weights, VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(pd), &pd);

        if (offsets_dispatched) {
            nb_buf_bar = 0;
            buf_bar[nb_buf_bar++] = (VkBufferMemoryBarrier2) {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = integral_vk->stage,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask = integral_vk->access,
                .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = integral_vk->buf,
                .size = integral_vk->size,
                .offset = 0,
            };

            vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
                    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .pBufferMemoryBarriers = buf_bar,
                    .bufferMemoryBarrierCount = nb_buf_bar,
                });
            integral_vk->stage = buf_bar[1].dstStageMask;
            integral_vk->access = buf_bar[1].dstAccessMask;
        }

        wg_invoc = FFMIN((s->nb_offsets - offsets_dispatched)/TYPE_ELEMS, s->opts.t);
        wg_invoc = FFMIN(wg_invoc, vkctx->props.properties.limits.maxComputeWorkGroupCount[2]);

        /* End of horizontal pass */
        vk->CmdDispatch(exec->buf, 1, 1, wg_invoc);

        offsets_dispatched += wg_invoc * TYPE_ELEMS;
    } while (offsets_dispatched < s->nb_offsets);

    RET(denoise_pass(s, exec, ws_vk, ws_stride));

    err = ff_vk_exec_submit(vkctx, exec);
    if (err < 0)
        return err;

    err = av_frame_copy_props(out, in);
    if (err < 0)
        goto fail;

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);

fail:
    av_buffer_unref(&integral_buf);
    av_buffer_unref(&ws_buf);
    av_frame_free(&in);
    av_frame_free(&out);
    return err;
}

static void nlmeans_vulkan_uninit(AVFilterContext *avctx)
{
    NLMeansVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;

    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_pipeline_free(vkctx, &s->pl_weights);
    ff_vk_shader_free(vkctx, &s->shd_weights);
    ff_vk_pipeline_free(vkctx, &s->pl_denoise);
    ff_vk_shader_free(vkctx, &s->shd_denoise);

    av_buffer_pool_uninit(&s->integral_buf_pool);
    av_buffer_pool_uninit(&s->ws_buf_pool);

    if (s->sampler)
        vk->DestroySampler(vkctx->hwctx->act_dev, s->sampler,
                           vkctx->hwctx->alloc);

    ff_vk_uninit(&s->vkctx);

    av_freep(&s->xoffsets);
    av_freep(&s->yoffsets);

    s->initialized = 0;
}

#define OFFSET(x) offsetof(NLMeansVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption nlmeans_vulkan_options[] = {
    { "s",  "denoising strength for all components", OFFSET(opts.s), AV_OPT_TYPE_DOUBLE, { .dbl = 1.0 }, 1.0, 100.0, FLAGS },
    { "p",  "patch size for all components", OFFSET(opts.p), AV_OPT_TYPE_INT, { .i64 = 3*2+1 }, 0, 99, FLAGS },
    { "r",  "research window radius", OFFSET(opts.r), AV_OPT_TYPE_INT, { .i64 = 7*2+1 }, 0, 99, FLAGS },
    { "t",  "parallelism", OFFSET(opts.t), AV_OPT_TYPE_INT, { .i64 = 36 }, 1, 168, FLAGS },

    { "s1", "denoising strength for component 1", OFFSET(opts.sc[0]), AV_OPT_TYPE_DOUBLE, { .dbl = 1.0 }, 1.0, 100.0, FLAGS },
    { "s2", "denoising strength for component 2", OFFSET(opts.sc[1]), AV_OPT_TYPE_DOUBLE, { .dbl = 1.0 }, 1.0, 100.0, FLAGS },
    { "s3", "denoising strength for component 3", OFFSET(opts.sc[2]), AV_OPT_TYPE_DOUBLE, { .dbl = 1.0 }, 1.0, 100.0, FLAGS },
    { "s4", "denoising strength for component 4", OFFSET(opts.sc[3]), AV_OPT_TYPE_DOUBLE, { .dbl = 1.0 }, 1.0, 100.0, FLAGS },

    { "p1", "patch size for component 1", OFFSET(opts.pc[0]), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 99, FLAGS },
    { "p2", "patch size for component 2", OFFSET(opts.pc[1]), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 99, FLAGS },
    { "p3", "patch size for component 3", OFFSET(opts.pc[2]), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 99, FLAGS },
    { "p4", "patch size for component 4", OFFSET(opts.pc[3]), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 99, FLAGS },

    { NULL }
};

AVFILTER_DEFINE_CLASS(nlmeans_vulkan);

static const AVFilterPad nlmeans_vulkan_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &nlmeans_vulkan_filter_frame,
        .config_props = &ff_vk_filter_config_input,
    },
};

static const AVFilterPad nlmeans_vulkan_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vk_filter_config_output,
    },
};

const AVFilter ff_vf_nlmeans_vulkan = {
    .name           = "nlmeans_vulkan",
    .description    = NULL_IF_CONFIG_SMALL("Non-local means denoiser (Vulkan)"),
    .priv_size      = sizeof(NLMeansVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &nlmeans_vulkan_uninit,
    FILTER_INPUTS(nlmeans_vulkan_inputs),
    FILTER_OUTPUTS(nlmeans_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .priv_class     = &nlmeans_vulkan_class,
    .flags          = AVFILTER_FLAG_HWDEVICE,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
