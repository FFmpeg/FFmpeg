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
#include "libavutil/vulkan_spirv.h"
#include "libavutil/opt.h"
#include "vulkan_filter.h"

#include "filters.h"
#include "video.h"

#define TYPE_NAME  "vec4"
#define TYPE_ELEMS 4
#define TYPE_SIZE  (TYPE_ELEMS*4)
#define TYPE_BLOCK_ELEMS 16
#define TYPE_BLOCK_SIZE (TYPE_SIZE * TYPE_BLOCK_ELEMS)
#define WG_SIZE 32

typedef struct NLMeansVulkanContext {
    FFVulkanContext vkctx;

    int initialized;
    FFVkExecPool e;
    AVVulkanDeviceQueueFamily *qf;

    AVBufferPool *integral_buf_pool;
    AVBufferPool *ws_buf_pool;

    FFVkBuffer xyoffsets_buf;

    FFVulkanShader shd_horizontal;
    FFVulkanShader shd_vertical;
    FFVulkanShader shd_weights;
    FFVulkanShader shd_denoise;

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

typedef struct IntegralPushData {
    uint32_t width[4];
    uint32_t height[4];
    float    strength[4];
    uint32_t comp_off[4];
    uint32_t comp_plane[4];
    VkDeviceAddress integral_base;
    uint64_t integral_size;
    uint64_t int_stride;
    uint32_t xyoffs_start;
    uint32_t nb_components;
} IntegralPushData;

static void shared_shd_def(FFVulkanShader *shd) {
    GLSLC(0, #extension GL_ARB_gpu_shader_int64 : require                     );
    GLSLC(0,                                                                  );
    GLSLF(0, #define DTYPE %s                                                 ,TYPE_NAME);
    GLSLF(0, #define T_ALIGN %i                                               ,TYPE_SIZE);
    GLSLF(0, #define T_BLOCK_ELEMS %i                                         ,TYPE_BLOCK_ELEMS);
    GLSLF(0, #define T_BLOCK_ALIGN %i                                         ,TYPE_BLOCK_SIZE);
    GLSLC(0,                                                                  );
    GLSLC(0, layout(buffer_reference, buffer_reference_align = T_ALIGN) buffer DataBuffer {  );
    GLSLC(1,     DTYPE v[];                                                   );
    GLSLC(0, };                                                               );
    GLSLC(0, struct Block {                                                   );
    GLSLC(1,     DTYPE data[T_BLOCK_ELEMS];                                   );
    GLSLC(0, };                                                               );
    GLSLC(0, layout(buffer_reference, buffer_reference_align = T_BLOCK_ALIGN) buffer BlockBuffer {  );
    GLSLC(1,     Block v[];                                                   );
    GLSLC(0, };                                                               );
    GLSLC(0, layout(push_constant, std430) uniform pushConstants {            );
    GLSLC(1,     uvec4 width;                                                 );
    GLSLC(1,     uvec4 height;                                                );
    GLSLC(1,     vec4 strength;                                               );
    GLSLC(1,     uvec4 comp_off;                                              );
    GLSLC(1,     uvec4 comp_plane;                                            );
    GLSLC(1,     DataBuffer integral_base;                                    );
    GLSLC(1,     uint64_t integral_size;                                      );
    GLSLC(1,     uint64_t int_stride;                                         );
    GLSLC(1,     uint xyoffs_start;                                           );
    GLSLC(1,     uint nb_components;                                          );
    GLSLC(0, };                                                               );
    GLSLC(0,                                                                  );

    ff_vk_shader_add_push_const(shd, 0, sizeof(IntegralPushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);
}

static av_cold int init_integral_pipeline(FFVulkanContext *vkctx, FFVkExecPool *exec,
                                          FFVulkanShader *shd_horizontal,
                                          FFVulkanShader *shd_vertical,
                                          FFVkSPIRVCompiler *spv,
                                          const AVPixFmtDescriptor *desc, int planes)
{
    int err;
    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    FFVulkanShader *shd;
    FFVulkanDescriptorSetBinding *desc_set;

    shd = shd_horizontal;
    RET(ff_vk_shader_init(vkctx, shd, "nlmeans_horizontal",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          (const char *[]) { "GL_EXT_buffer_reference",
                                             "GL_EXT_buffer_reference2" }, 2,
                          WG_SIZE, 1, 1,
                          0));
    shared_shd_def(shd);

    GLSLC(0,                                                                     );
    GLSLC(0, void main()                                                         );
    GLSLC(0, {                                                                   );
    GLSLC(1,     uint64_t offset;                                                );
    GLSLC(1,     DataBuffer dst;                                                 );
    GLSLC(1,     BlockBuffer b_dst;                                              );
    GLSLC(1,     Block block;                                                    );
    GLSLC(1,     DTYPE s2;                                                       );
    GLSLC(1,     DTYPE prefix_sum;                                               );
    GLSLC(1,     ivec2 pos;                                                      );
    GLSLC(1,     int k;                                                          );
    GLSLC(1,     int o;                                                          );
    GLSLC(0,                                                                     );
    GLSLC(1,     DataBuffer integral_data;                                       );
    GLSLC(0,                                                                     );
    GLSLC(1,     uint c_plane;                                                   );
    GLSLC(0,                                                                     );
    GLSLC(1,     uint comp_idx = uint(gl_WorkGroupID.y);                         );
    GLSLC(1,     uint invoc_idx = uint(gl_WorkGroupID.z);                        );
    GLSLC(0,                                                                     );
    GLSLC(1,     if (strength[comp_idx] == 0.0)                                  );
    GLSLC(2,         return;                                                     );
    GLSLC(0,                                                                     );
    GLSLC(1,     offset = integral_size * (invoc_idx * nb_components + comp_idx); );
    GLSLC(1,     integral_data = DataBuffer(uint64_t(integral_base) + offset);   );
    GLSLC(0,                                                                     );
    GLSLC(1,     c_plane = comp_plane[comp_idx];                                 );
    GLSLC(0,                                                                     );
    GLSLC(1,     pos.y = int(gl_GlobalInvocationID.x);                           );
    GLSLC(1,     if (pos.y < height[c_plane]) {                                  );
    GLSLC(2,         prefix_sum = DTYPE(0);                                      );
    GLSLC(2,         offset = int_stride * uint64_t(pos.y);                      );
    GLSLC(2,         b_dst = BlockBuffer(uint64_t(integral_data) + offset);      );
    GLSLC(0,                                                                     );
    GLSLC(2,         for (k = 0; k * T_BLOCK_ELEMS < width[c_plane]; k++) {      );
    GLSLC(3,             block = b_dst.v[k];                                     );
    GLSLC(3,             for (o = 0; o < T_BLOCK_ELEMS; o++) {                   );
    GLSLC(4,                 s2 = block.data[o];                                 );
    GLSLC(4,                 block.data[o] = s2 + prefix_sum;                    );
    GLSLC(4,                 prefix_sum += s2;                                   );
    GLSLC(3,             }                                                       );
    GLSLC(3,             b_dst.v[k] = block;                                     );
    GLSLC(2,         }                                                           );
    GLSLC(1,     }                                                               );
    GLSLC(0, }                                                                   );

    RET(spv->compile_shader(vkctx, spv, shd, &spv_data, &spv_len, "main", &spv_opaque));
    RET(ff_vk_shader_link(vkctx, shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, exec, shd));

    shd = shd_vertical;
    RET(ff_vk_shader_init(vkctx, shd, "nlmeans_vertical",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          (const char *[]) { "GL_EXT_buffer_reference",
                                             "GL_EXT_buffer_reference2" }, 2,
                          WG_SIZE, 1, 1,
                          0));
    shared_shd_def(shd);

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name       = "input_img",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .mem_layout = ff_vk_shader_rep_fmt(vkctx->input_format, FF_VK_REP_FLOAT),
            .mem_quali  = "readonly",
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    RET(ff_vk_shader_add_descriptor_set(vkctx, shd, desc_set, 1, 0, 0));

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "xyoffsets_buffer",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .mem_quali   = "readonly",
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "ivec2 xyoffsets[];",
        },
    };
    RET(ff_vk_shader_add_descriptor_set(vkctx, shd, desc_set, 1, 1, 0));

    GLSLC(0,                                                                     );
    GLSLC(0, void main()                                                         );
    GLSLC(0, {                                                                   );
    GLSLC(1,     uint64_t offset;                                                );
    GLSLC(1,     DataBuffer dst;                                                 );
    GLSLC(1,     float s1;                                                       );
    GLSLC(1,     DTYPE s2;                                                       );
    GLSLC(1,     DTYPE prefix_sum;                                               );
    GLSLC(1,     uvec2 size;                                                     );
    GLSLC(1,     ivec2 pos;                                                      );
    GLSLC(1,     ivec2 pos_off;                                                  );
    GLSLC(0,                                                                     );
    GLSLC(1,     DataBuffer integral_data;                                       );
    GLSLF(1,     ivec2 offs[%i];                                                 ,TYPE_ELEMS);
    GLSLC(0,                                                                     );
    GLSLC(1,     uint c_off;                                                     );
    GLSLC(1,     uint c_plane;                                                   );
    GLSLC(0,                                                                     );
    GLSLC(1,     uint comp_idx = uint(gl_WorkGroupID.y);                         );
    GLSLC(1,     uint invoc_idx = uint(gl_WorkGroupID.z);                        );
    GLSLC(0,                                                                     );
    GLSLC(1,     if (strength[comp_idx] == 0.0)                                  );
    GLSLC(2,         return;                                                     );
    GLSLC(0,                                                                     );
    GLSLC(1,     offset = integral_size * (invoc_idx * nb_components + comp_idx); );
    GLSLC(1,     integral_data = DataBuffer(uint64_t(integral_base) + offset);   );
    for (int i = 0; i < TYPE_ELEMS; i++)
        GLSLF(1, offs[%i] = xyoffsets[xyoffs_start + %i*invoc_idx + %i];         ,i,TYPE_ELEMS,i);
    GLSLC(0,                                                                     );
    GLSLC(1,     c_off = comp_off[comp_idx];                                     );
    GLSLC(1,     c_plane = comp_plane[comp_idx];                                 );
    GLSLC(1,     size = imageSize(input_img[c_plane]);                           );
    GLSLC(0,                                                                     );
    GLSLC(1,     pos.x = int(gl_GlobalInvocationID.x);                           );
    GLSLC(1,     if (pos.x < width[c_plane]) {                                   );
    GLSLC(2,         prefix_sum = DTYPE(0);                                      );
    GLSLC(2,         for (pos.y = 0; pos.y < height[c_plane]; pos.y++) {         );
    GLSLC(3,             offset = int_stride * uint64_t(pos.y);                  );
    GLSLC(3,             dst = DataBuffer(uint64_t(integral_data) + offset);     );
    GLSLC(4,             s1 = imageLoad(input_img[c_plane], pos)[c_off];         );
    for (int i = 0; i < TYPE_ELEMS; i++) {
        GLSLF(4,         pos_off = pos + offs[%i];                               ,i);
        GLSLC(4,         if (!IS_WITHIN(uvec2(pos_off), size))                   );
        GLSLF(5,             s2[%i] = s1;                                        ,i);
        GLSLC(4,         else                                                    );
        GLSLF(5,             s2[%i] = imageLoad(input_img[c_plane], pos_off)[c_off]; ,i);
    }
    GLSLC(4,             s2 = (s1 - s2) * (s1 - s2);                             );
    GLSLC(3,             dst.v[pos.x] = s2 + prefix_sum;                         );
    GLSLC(3,             prefix_sum += s2;                                       );
    GLSLC(2,         }                                                           );
    GLSLC(1,     }                                                               );
    GLSLC(0, }                                                                   );

    RET(spv->compile_shader(vkctx, spv, shd, &spv_data, &spv_len, "main", &spv_opaque));
    RET(ff_vk_shader_link(vkctx, shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, exec, shd));

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);

    return err;
}

typedef struct WeightsPushData {
    uint32_t width[4];
    uint32_t height[4];
    uint32_t ws_offset[4];
    uint32_t ws_stride[4];
    int32_t  patch_size[4];
    float    strength[4];
    uint32_t comp_off[4];
    uint32_t comp_plane[4];
    VkDeviceAddress integral_base;
    uint64_t integral_size;
    uint64_t int_stride;
    uint32_t xyoffs_start;
    uint32_t ws_count;
    uint32_t nb_components;
} WeightsPushData;

static av_cold int init_weights_pipeline(FFVulkanContext *vkctx, FFVkExecPool *exec,
                                         FFVulkanShader *shd,
                                         FFVkSPIRVCompiler *spv,
                                         const AVPixFmtDescriptor *desc,
                                         int planes)
{
    int err;
    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    FFVulkanDescriptorSetBinding *desc_set;

    RET(ff_vk_shader_init(vkctx, shd, "nlmeans_weights",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          (const char *[]) { "GL_EXT_buffer_reference",
                                             "GL_EXT_buffer_reference2" }, 2,
                          WG_SIZE, WG_SIZE, 1,
                          0));

    GLSLC(0, #extension GL_ARB_gpu_shader_int64 : require                     );
    GLSLC(0,                                                                  );
    GLSLF(0, #define DTYPE %s                                                 ,TYPE_NAME);
    GLSLF(0, #define T_ALIGN %i                                               ,TYPE_SIZE);
    GLSLC(0,                                                                  );
    GLSLC(0, layout(buffer_reference, buffer_reference_align = T_ALIGN) buffer DataBuffer {  );
    GLSLC(1,     DTYPE v[];                                                   );
    GLSLC(0, };                                                               );
    GLSLC(0, layout(push_constant, std430) uniform pushConstants {            );
    GLSLC(1,     uvec4 width;                                                 );
    GLSLC(1,     uvec4 height;                                                );
    GLSLC(1,     uvec4 ws_offset;                                             );
    GLSLC(1,     uvec4 ws_stride;                                             );
    GLSLC(1,     ivec4 patch_size;                                            );
    GLSLC(1,     vec4 strength;                                               );
    GLSLC(1,     uvec4 comp_off;                                              );
    GLSLC(1,     uvec4 comp_plane;                                            );
    GLSLC(1,     DataBuffer integral_base;                                    );
    GLSLC(1,     uint64_t integral_size;                                      );
    GLSLC(1,     uint64_t int_stride;                                         );
    GLSLC(1,     uint xyoffs_start;                                           );
    GLSLC(1,     uint ws_count;                                               );
    GLSLC(1,     uint nb_components;                                          );
    GLSLC(0, };                                                               );
    GLSLC(0,                                                                  );

    ff_vk_shader_add_push_const(shd, 0, sizeof(WeightsPushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name       = "input_img",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .mem_layout = ff_vk_shader_rep_fmt(vkctx->input_format, FF_VK_REP_FLOAT),
            .mem_quali  = "readonly",
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name        = "weights_buffer",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float weights[];",
        },
        {
            .name        = "sums_buffer",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float sums[];",
        },
    };
    RET(ff_vk_shader_add_descriptor_set(vkctx, shd, desc_set, 3, 0, 0));

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "xyoffsets_buffer",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .mem_quali   = "readonly",
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "ivec2 xyoffsets[];",
        },
    };
    RET(ff_vk_shader_add_descriptor_set(vkctx, shd, desc_set, 1, 1, 0));

    GLSLC(0,                                                                     );
    GLSLC(0, void main()                                                         );
    GLSLC(0, {                                                                   );
    GLSLC(1,     uint64_t offset;                                                );
    GLSLC(1,     DataBuffer dst;                                                 );
    GLSLC(1,     uvec2 size;                                                     );
    GLSLC(1,     ivec2 pos;                                                      );
    GLSLC(1,     ivec2 pos_off;                                                  );
    GLSLC(1,     int p;                                                          );
    GLSLC(1,     float s;                                                        );
    GLSLC(0,                                                                     );
    GLSLC(1,     DataBuffer integral_data;                                       );
    GLSLF(1,     ivec2 offs[%i];                                                 ,TYPE_ELEMS);
    GLSLC(0,                                                                     );
    GLSLC(1,     uint c_off;                                                     );
    GLSLC(1,     uint c_plane;                                                   );
    GLSLC(1,     uint ws_off;                                                    );
    GLSLC(0,                                                                     );
    GLSLC(1,     pos = ivec2(gl_GlobalInvocationID.xy);                          );
    GLSLC(1,     uint comp_idx = uint(gl_WorkGroupID.z) %% nb_components;        );
    GLSLC(1,     uint invoc_idx = uint(gl_WorkGroupID.z) / nb_components;        );
    GLSLC(0,                                                                     );
    GLSLC(1,     c_off = comp_off[comp_idx];                                     );
    GLSLC(1,     c_plane = comp_plane[comp_idx];                                 );
    GLSLC(1,     p = patch_size[comp_idx];                                       );
    GLSLC(1,     s = strength[comp_idx];                                         );
    GLSLC(1,     if (s == 0.0 || pos.x < p || pos.y < p || pos.x >= width[c_plane] - p || pos.y >= height[c_plane] - p) );
    GLSLC(2,         return;                                                     );
    GLSLC(0,                                                                     );
    GLSLC(1,     offset = integral_size * (invoc_idx * nb_components + comp_idx); );
    GLSLC(1,     integral_data = DataBuffer(uint64_t(integral_base) + offset);   );
    for (int i = 0; i < TYPE_ELEMS; i++)
        GLSLF(1, offs[%i] = xyoffsets[xyoffs_start + %i*invoc_idx + %i];         ,i,TYPE_ELEMS,i);
    GLSLC(0,                                                                     );
    GLSLC(1,     ws_off = ws_count * invoc_idx + ws_offset[comp_idx] + pos.y * ws_stride[comp_idx] + pos.x; );
    GLSLC(1,     size = imageSize(input_img[c_plane]);                           );
    GLSLC(0,                                                                     );
    GLSLC(1,     DTYPE a;                                                        );
    GLSLC(1,     DTYPE b;                                                        );
    GLSLC(1,     DTYPE c;                                                        );
    GLSLC(1,     DTYPE d;                                                        );
    GLSLC(0,                                                                     );
    GLSLC(1,     DTYPE patch_diff;                                               );
    GLSLC(1,     vec4 src;                                                       );
    GLSLC(1,     vec4 w;                                                         );
    GLSLC(1,     float w_sum;                                                    );
    GLSLC(1,     float sum;                                                      );
    GLSLC(0,                                                                     );
    for (int i = 0; i < 4; i++) {
        GLSLF(1,     pos_off = pos + offs[%i];                                   ,i);
        GLSLC(1,     if (!IS_WITHIN(uvec2(pos_off), size))                       );
        GLSLF(2,         src[%i] = imageLoad(input_img[c_plane], pos)[c_off];    ,i);
        GLSLC(1,     else                                                        );
        GLSLF(2,         src[%i] = imageLoad(input_img[c_plane], pos_off)[c_off]; ,i);
    }
    GLSLC(0,                                                                     );
    GLSLC(1,         offset = int_stride * uint64_t(pos.y - p);                  );
    GLSLC(1,         dst = DataBuffer(uint64_t(integral_data) + offset);         );
    GLSLC(1,         a = dst.v[pos.x - p];                                       );
    GLSLC(1,         c = dst.v[pos.x + p];                                       );
    GLSLC(1,         offset = int_stride * uint64_t(pos.y + p);                  );
    GLSLC(1,         dst = DataBuffer(uint64_t(integral_data) + offset);         );
    GLSLC(1,         b = dst.v[pos.x - p];                                       );
    GLSLC(1,         d = dst.v[pos.x + p];                                       );
    GLSLC(0,                                                                     );
    GLSLC(1,         patch_diff = d + a - b - c;                                 );
    GLSLC(1,         w = exp(patch_diff * s);                                    );
    GLSLC(1,         w_sum = w[0] + w[1] + w[2] + w[3];                          );
    GLSLC(1,         sum = dot(w, src * 255);                                    );
    GLSLC(0,                                                                     );
    GLSLC(1,         weights[ws_off] += w_sum;                                   );
    GLSLC(1,         sums[ws_off] += sum;                                        );
    GLSLC(0, }                                                                   );

    RET(spv->compile_shader(vkctx, spv, shd, &spv_data, &spv_len, "main", &spv_opaque));
    RET(ff_vk_shader_link(vkctx, shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, exec, shd));

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);

    return err;
}

typedef struct DenoisePushData {
    uint32_t comp_off[4];
    uint32_t comp_plane[4];
    uint32_t ws_offset[4];
    uint32_t ws_stride[4];
    uint32_t ws_count;
    uint32_t t;
    uint32_t nb_components;
} DenoisePushData;

static av_cold int init_denoise_pipeline(FFVulkanContext *vkctx, FFVkExecPool *exec,
                                         FFVulkanShader *shd, FFVkSPIRVCompiler *spv,
                                         const AVPixFmtDescriptor *desc, int planes)
{
    int err;
    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    FFVulkanDescriptorSetBinding *desc_set;
    RET(ff_vk_shader_init(vkctx, shd, "nlmeans_denoise",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          (const char *[]) { "GL_EXT_buffer_reference",
                                             "GL_EXT_buffer_reference2" }, 2,
                          WG_SIZE, WG_SIZE, 1,
                          0));

    GLSLC(0, layout(push_constant, std430) uniform pushConstants {        );
    GLSLC(1,    uvec4 comp_off;                                           );
    GLSLC(1,    uvec4 comp_plane;                                         );
    GLSLC(1,    uvec4 ws_offset;                                          );
    GLSLC(1,    uvec4 ws_stride;                                          );
    GLSLC(1,    uint32_t ws_count;                                        );
    GLSLC(1,    uint32_t t;                                               );
    GLSLC(1,    uint32_t nb_components;                                   );
    GLSLC(0, };                                                           );

    ff_vk_shader_add_push_const(shd, 0, sizeof(DenoisePushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "input_img",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .mem_layout  = ff_vk_shader_rep_fmt(vkctx->input_format, FF_VK_REP_FLOAT),
            .mem_quali   = "readonly",
            .dimensions  = 2,
            .elems       = planes,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .name        = "output_img",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .mem_layout  = ff_vk_shader_rep_fmt(vkctx->output_format, FF_VK_REP_FLOAT),
            .mem_quali   = "writeonly",
            .dimensions  = 2,
            .elems       = planes,
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    RET(ff_vk_shader_add_descriptor_set(vkctx, shd, desc_set, 2, 0, 0));

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name        = "weights_buffer",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .mem_quali   = "readonly",
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float weights[];",
        },
        {
            .name        = "sums_buffer",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .mem_quali   = "readonly",
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .buf_content = "float sums[];",
        },
    };

    RET(ff_vk_shader_add_descriptor_set(vkctx, shd, desc_set, 2, 0, 0));

    GLSLC(0, void main()                                                      );
    GLSLC(0, {                                                                );
    GLSLC(1,     const ivec2 pos = ivec2(gl_GlobalInvocationID.xy);           );
    GLSLC(1,     const uint plane = uint(gl_WorkGroupID.z);                   );
    GLSLC(1,     const uvec2 size = imageSize(output_img[plane]);             );
    GLSLC(0,                                                                  );
    GLSLC(1,     uint c_off;                                                  );
    GLSLC(1,     uint c_plane;                                                );
    GLSLC(1,     uint ws_off;                                                 );
    GLSLC(0,                                                                  );
    GLSLC(1,     float w_sum;                                                 );
    GLSLC(1,     float sum;                                                   );
    GLSLC(1,     vec4 src;                                                    );
    GLSLC(1,     vec4 r;                                                      );
    GLSLC(1,     uint invoc_idx;                                              );
    GLSLC(1,     uint comp_idx;                                               );
    GLSLC(0,                                                                  );
    GLSLC(1,     if (!IS_WITHIN(pos, size))                                   );
    GLSLC(2,         return;                                                  );
    GLSLC(0,                                                                  );
    GLSLC(1,     src = imageLoad(input_img[plane], pos);                      );
    GLSLC(1,     for (comp_idx = 0; comp_idx < nb_components; comp_idx++) {   );
    GLSLC(2,         if (plane == comp_plane[comp_idx]) {                     );
    GLSLC(3,             w_sum = 0.0;                                         );
    GLSLC(3,             sum = 0.0;                                           );
    GLSLC(3,             for (invoc_idx = 0; invoc_idx < t; invoc_idx++) {    );
    GLSLC(4,                 ws_off = ws_count * invoc_idx + ws_offset[comp_idx] + pos.y * ws_stride[comp_idx] + pos.x; );
    GLSLC(4,                 w_sum += weights[ws_off];                        );
    GLSLC(4,                 sum += sums[ws_off];                             );
    GLSLC(3,             }                                                    );
    GLSLC(3,             c_off = comp_off[comp_idx];                          );
    GLSLC(3,             r[c_off] = (sum + src[c_off] * 255) / (1.0 + w_sum) / 255; );
    GLSLC(2,         }                                                        );
    GLSLC(1,     }                                                            );
    GLSLC(1,     imageStore(output_img[plane], pos, r);                       );
    GLSLC(0, }                                                                );

    RET(spv->compile_shader(vkctx, spv, shd, &spv_data, &spv_len, "main", &spv_opaque));
    RET(ff_vk_shader_link(vkctx, shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, exec, shd));

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
        double str = !isnan(s->opts.sc[i]) ? s->opts.sc[i] : s->opts.s;
        int ps = (s->opts.pc[i] ? s->opts.pc[i] : s->opts.p);
        if (str == 0.0) {
            s->strength[i] = 0.0;
        } else {
            str  = 10.0f*str;
            str *= -str;
            str  = 255.0*255.0 / str;
            s->strength[i] = str;
        }
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

    spv = ff_vk_spirv_init();
    if (!spv) {
        av_log(ctx, AV_LOG_ERROR, "Unable to initialize SPIR-V compiler!\n");
        return AVERROR_EXTERNAL;
    }

    s->qf = ff_vk_qf_find(vkctx, VK_QUEUE_COMPUTE_BIT, 0);
    if (!s->qf) {
        av_log(ctx, AV_LOG_ERROR, "Device has no compute queues\n");
        err = AVERROR(ENOTSUP);
        goto fail;
    }

    RET(ff_vk_exec_pool_init(vkctx, s->qf, &s->e, 1, 0, 0, 0, NULL));

    RET(init_integral_pipeline(vkctx, &s->e, &s->shd_horizontal, &s->shd_vertical,
                               spv, desc, planes));

    RET(init_weights_pipeline(vkctx, &s->e, &s->shd_weights, spv, desc, planes));

    RET(init_denoise_pipeline(vkctx, &s->e, &s->shd_denoise, spv, desc, planes));

    RET(ff_vk_shader_update_desc_buffer(vkctx, &s->e.contexts[0], &s->shd_vertical,
                                        1, 0, 0,
                                        &s->xyoffsets_buf, 0, s->xyoffsets_buf.size,
                                        VK_FORMAT_UNDEFINED));

    RET(ff_vk_shader_update_desc_buffer(vkctx, &s->e.contexts[0], &s->shd_weights,
                                        1, 0, 0,
                                        &s->xyoffsets_buf, 0, s->xyoffsets_buf.size,
                                        VK_FORMAT_UNDEFINED));

    do {
        int wg_invoc = FFMIN((s->nb_offsets - offsets_dispatched)/TYPE_ELEMS, s->opts.t);
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
                        FFVkBuffer *ws_vk, uint32_t comp_offs[4], uint32_t comp_planes[4],
                        uint32_t ws_offset[4], uint32_t ws_stride[4],
                        uint32_t ws_count, uint32_t t, uint32_t nb_components)
{
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;
    VkBufferMemoryBarrier2 buf_bar[2];
    int nb_buf_bar = 0;

    DenoisePushData pd = {
        { comp_offs[0], comp_offs[1], comp_offs[2], comp_offs[3] },
        { comp_planes[0], comp_planes[1], comp_planes[2], comp_planes[3] },
        { ws_offset[0], ws_offset[1], ws_offset[2], ws_offset[3] },
        { ws_stride[0], ws_stride[1], ws_stride[2], ws_stride[3] },
        ws_count,
        t,
        nb_components,
    };

    /* Denoise pass pipeline */
    ff_vk_exec_bind_shader(vkctx, exec, &s->shd_denoise);

    /* Push data */
    ff_vk_shader_update_push_const(vkctx, exec, &s->shd_denoise,
                                   VK_SHADER_STAGE_COMPUTE_BIT,
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
                    FFALIGN(vkctx->output_width,  s->shd_denoise.lg_size[0])/s->shd_denoise.lg_size[0],
                    FFALIGN(vkctx->output_height, s->shd_denoise.lg_size[1])/s->shd_denoise.lg_size[1],
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
    int comp_offs[4];
    int comp_planes[4];
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
    uint32_t ws_count = 0;
    uint32_t ws_offset[4];
    uint32_t ws_stride[4];
    size_t ws_size;

    FFVkExecContext *exec;
    VkImageView in_views[AV_NUM_DATA_POINTERS];
    VkImageView out_views[AV_NUM_DATA_POINTERS];
    VkImageMemoryBarrier2 img_bar[8];
    int nb_img_bar = 0;
    VkBufferMemoryBarrier2 buf_bar[2];
    int nb_buf_bar = 0;

    if (!s->initialized)
        RET(init_filter(ctx));

    desc = av_pix_fmt_desc_get(vkctx->output_format);
    if (!desc)
        return AVERROR(EINVAL);

    /* Integral image */
    int_stride = FFALIGN(vkctx->output_width, s->shd_vertical.lg_size[0]) * TYPE_SIZE;
    int_size = FFALIGN(vkctx->output_height, s->shd_horizontal.lg_size[0]) * int_stride;

    /* Plane dimensions */
    for (int i = 0; i < desc->nb_components; i++) {
        plane_widths[i] = !i || (i == 3) ? vkctx->output_width : AV_CEIL_RSHIFT(vkctx->output_width, desc->log2_chroma_w);
        plane_heights[i] = !i || (i == 3) ? vkctx->output_height : AV_CEIL_RSHIFT(vkctx->output_height, desc->log2_chroma_h);
        plane_widths[i]  = FFALIGN(plane_widths[i],  s->shd_denoise.lg_size[0]);
        plane_heights[i] = FFALIGN(plane_heights[i], s->shd_denoise.lg_size[1]);

        comp_offs[i] = desc->comp[i].offset / (FFALIGN(desc->comp[i].depth, 8)/8);
        comp_planes[i] = desc->comp[i].plane;

        ws_stride[i] = plane_widths[i];
        ws_offset[i] = ws_count;
        ws_count += ws_stride[i] * plane_heights[i];
    }

    ws_size = ws_count * sizeof(float);

    /* Buffers */
    err = ff_vk_get_pooled_buffer(&s->vkctx, &s->integral_buf_pool, &integral_buf,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                  NULL,
                                  int_size * s->opts.t * desc->nb_components,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (err < 0)
        return err;
    integral_vk = (FFVkBuffer *)integral_buf->data;

    err = ff_vk_get_pooled_buffer(&s->vkctx, &s->ws_buf_pool, &ws_buf,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                  NULL,
                                  ws_size * s-> opts.t * 2,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (err < 0)
        return err;
    ws_vk = (FFVkBuffer *)ws_buf->data;

    /* Output frame */
    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    /* Execution context */
    exec = ff_vk_exec_get(&s->vkctx, &s->e);
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
    RET(ff_vk_create_imageviews(vkctx, exec, in_views, in, FF_VK_REP_FLOAT));
    ff_vk_frame_barrier(vkctx, exec, in, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_QUEUE_FAMILY_IGNORED);

    /* Output frame prep */
    RET(ff_vk_create_imageviews(vkctx, exec, out_views, out, FF_VK_REP_FLOAT));
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

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pImageMemoryBarriers = img_bar,
            .imageMemoryBarrierCount = nb_img_bar,
            .pBufferMemoryBarriers = buf_bar,
            .bufferMemoryBarrierCount = nb_buf_bar,
        });
    ws_vk->stage = buf_bar[0].dstStageMask;
    ws_vk->access = buf_bar[0].dstAccessMask;

    /* Buffer zeroing */
    vk->CmdFillBuffer(exec->buf, ws_vk->buf, 0, ws_vk->size, 0x0);

    /* Update integral descriptors */
    ff_vk_shader_update_img_array(vkctx, exec, &s->shd_vertical, in, in_views, 0, 0,
                                  VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE);
    /* Update weights descriptors */
    ff_vk_shader_update_img_array(vkctx, exec, &s->shd_weights, in, in_views, 0, 0,
                                  VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE);
    RET(ff_vk_shader_update_desc_buffer(&s->vkctx, exec, &s->shd_weights, 0, 1, 0,
                                        ws_vk, 0, ws_size * s-> opts.t,
                                        VK_FORMAT_UNDEFINED));
    RET(ff_vk_shader_update_desc_buffer(&s->vkctx, exec, &s->shd_weights, 0, 2, 0,
                                        ws_vk, ws_size * s-> opts.t, ws_size * s-> opts.t,
                                        VK_FORMAT_UNDEFINED));

    /* Update denoise descriptors */
    ff_vk_shader_update_img_array(vkctx, exec, &s->shd_denoise, in, in_views, 0, 0,
                                  VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE);
    ff_vk_shader_update_img_array(vkctx, exec, &s->shd_denoise, out, out_views, 0, 1,
                                  VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE);
    RET(ff_vk_shader_update_desc_buffer(&s->vkctx, exec, &s->shd_denoise, 1, 0, 0,
                                        ws_vk, 0, ws_size * s-> opts.t,
                                        VK_FORMAT_UNDEFINED));
    RET(ff_vk_shader_update_desc_buffer(&s->vkctx, exec, &s->shd_denoise, 1, 1, 0,
                                        ws_vk, ws_size * s-> opts.t, ws_size * s-> opts.t,
                                        VK_FORMAT_UNDEFINED));

    do {
        int wg_invoc = FFMIN((s->nb_offsets - offsets_dispatched)/TYPE_ELEMS, s->opts.t);

        /* Integral pipeline */
        IntegralPushData pd = {
            { plane_widths[0], plane_widths[1], plane_widths[2], plane_widths[3] },
            { plane_heights[0], plane_heights[1], plane_heights[2], plane_heights[3] },
            { s->strength[0], s->strength[1], s->strength[2], s->strength[3], },
            { comp_offs[0], comp_offs[1], comp_offs[2], comp_offs[3] },
            { comp_planes[0], comp_planes[1], comp_planes[2], comp_planes[3] },
            integral_vk->address,
            (uint64_t)int_size,
            (uint64_t)int_stride,
            offsets_dispatched,
            desc->nb_components,
        };

        ff_vk_exec_bind_shader(vkctx, exec, &s->shd_vertical);
        ff_vk_shader_update_push_const(vkctx, exec, &s->shd_vertical,
                                       VK_SHADER_STAGE_COMPUTE_BIT,
                                       0, sizeof(pd), &pd);

        nb_buf_bar = 0;
        buf_bar[nb_buf_bar++] = (VkBufferMemoryBarrier2) {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = integral_vk->stage,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = integral_vk->access,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
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
        integral_vk->stage = buf_bar[0].dstStageMask;
        integral_vk->access = buf_bar[0].dstAccessMask;

        /* End of vertical pass */
        vk->CmdDispatch(exec->buf,
                        FFALIGN(vkctx->output_width, s->shd_vertical.lg_size[0])/s->shd_vertical.lg_size[0],
                        desc->nb_components,
                        wg_invoc);

        ff_vk_exec_bind_shader(vkctx, exec, &s->shd_horizontal);
        ff_vk_shader_update_push_const(vkctx, exec, &s->shd_horizontal,
                                       VK_SHADER_STAGE_COMPUTE_BIT,
                                       0, sizeof(pd), &pd);

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
        integral_vk->stage = buf_bar[0].dstStageMask;
        integral_vk->access = buf_bar[0].dstAccessMask;

        /* End of horizontal pass */
        vk->CmdDispatch(exec->buf,
                        FFALIGN(vkctx->output_height, s->shd_horizontal.lg_size[0])/s->shd_horizontal.lg_size[0],
                        desc->nb_components,
                        wg_invoc);

        /* Weights pipeline */
        WeightsPushData wpd = {
            { plane_widths[0], plane_widths[1], plane_widths[2], plane_widths[3] },
            { plane_heights[0], plane_heights[1], plane_heights[2], plane_heights[3] },
            { ws_offset[0], ws_offset[1], ws_offset[2], ws_offset[3] },
            { ws_stride[0], ws_stride[1], ws_stride[2], ws_stride[3] },
            { s->patch[0], s->patch[1], s->patch[2], s->patch[3] },
            { s->strength[0], s->strength[1], s->strength[2], s->strength[3], },
            { comp_offs[0], comp_offs[1], comp_offs[2], comp_offs[3] },
            { comp_planes[0], comp_planes[1], comp_planes[2], comp_planes[3] },
            integral_vk->address,
            (uint64_t)int_size,
            (uint64_t)int_stride,
            offsets_dispatched,
            ws_count,
            desc->nb_components,
        };

        ff_vk_exec_bind_shader(vkctx, exec, &s->shd_weights);
        ff_vk_shader_update_push_const(vkctx, exec, &s->shd_weights,
                                        VK_SHADER_STAGE_COMPUTE_BIT,
                                        0, sizeof(wpd), &wpd);

        nb_buf_bar = 0;
        buf_bar[nb_buf_bar++] = (VkBufferMemoryBarrier2) {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = integral_vk->stage,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = integral_vk->access,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = integral_vk->buf,
            .size = integral_vk->size,
            .offset = 0,
        };
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
        integral_vk->stage = buf_bar[0].dstStageMask;
        integral_vk->access = buf_bar[0].dstAccessMask;
        ws_vk->stage = buf_bar[1].dstStageMask;
        ws_vk->access = buf_bar[1].dstAccessMask;

        /* End of weights pass */
        vk->CmdDispatch(exec->buf,
                        FFALIGN(vkctx->output_width, s->shd_weights.lg_size[0])/s->shd_weights.lg_size[0],
                        FFALIGN(vkctx->output_height, s->shd_weights.lg_size[1])/s->shd_weights.lg_size[1],
                        wg_invoc * desc->nb_components);

        offsets_dispatched += wg_invoc * TYPE_ELEMS;
    } while (offsets_dispatched < s->nb_offsets);

    RET(denoise_pass(s, exec, ws_vk, comp_offs, comp_planes, ws_offset, ws_stride,
                     ws_count, s->opts.t, desc->nb_components));

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

    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_shader_free(vkctx, &s->shd_horizontal);
    ff_vk_shader_free(vkctx, &s->shd_vertical);
    ff_vk_shader_free(vkctx, &s->shd_weights);
    ff_vk_shader_free(vkctx, &s->shd_denoise);

    av_buffer_pool_uninit(&s->integral_buf_pool);
    av_buffer_pool_uninit(&s->ws_buf_pool);

    ff_vk_uninit(&s->vkctx);

    av_freep(&s->xoffsets);
    av_freep(&s->yoffsets);

    s->initialized = 0;
}

#define OFFSET(x) offsetof(NLMeansVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption nlmeans_vulkan_options[] = {
    { "s",  "denoising strength for all components", OFFSET(opts.s), AV_OPT_TYPE_DOUBLE, { .dbl = 1.0 }, 0.0, 100.0, FLAGS },
    { "p",  "patch size for all components", OFFSET(opts.p), AV_OPT_TYPE_INT, { .i64 = 3*2+1 }, 0, 99, FLAGS },
    { "r",  "research window size", OFFSET(opts.r), AV_OPT_TYPE_INT, { .i64 = 7*2+1 }, 0, 99, FLAGS },
    { "t",  "parallelism", OFFSET(opts.t), AV_OPT_TYPE_INT, { .i64 = 8 }, 1, 64, FLAGS },

    { "s1", "denoising strength for component 1", OFFSET(opts.sc[0]), AV_OPT_TYPE_DOUBLE, { .dbl = NAN }, 0.0, 100.0, FLAGS },
    { "s2", "denoising strength for component 2", OFFSET(opts.sc[1]), AV_OPT_TYPE_DOUBLE, { .dbl = NAN }, 0.0, 100.0, FLAGS },
    { "s3", "denoising strength for component 3", OFFSET(opts.sc[2]), AV_OPT_TYPE_DOUBLE, { .dbl = NAN }, 0.0, 100.0, FLAGS },
    { "s4", "denoising strength for component 4", OFFSET(opts.sc[3]), AV_OPT_TYPE_DOUBLE, { .dbl = NAN }, 0.0, 100.0, FLAGS },

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

const FFFilter ff_vf_nlmeans_vulkan = {
    .p.name         = "nlmeans_vulkan",
    .p.description  = NULL_IF_CONFIG_SMALL("Non-local means denoiser (Vulkan)"),
    .p.priv_class   = &nlmeans_vulkan_class,
    .p.flags        = AVFILTER_FLAG_HWDEVICE,
    .priv_size      = sizeof(NLMeansVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &nlmeans_vulkan_uninit,
    FILTER_INPUTS(nlmeans_vulkan_inputs),
    FILTER_OUTPUTS(nlmeans_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
