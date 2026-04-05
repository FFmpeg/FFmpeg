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

#include "libavutil/opt.h"
#include "vulkan_filter.h"

#include "v360.h"
#include "filters.h"
#include "video.h"

extern const unsigned char ff_v360_comp_spv_data[];
extern const unsigned int ff_v360_comp_spv_len;

typedef struct V360ulkanContext {
    FFVulkanContext vkctx;

    int initialized;
    FFVkExecPool e;
    AVVulkanDeviceQueueFamily *qf;
    FFVulkanShader shd;
    VkSampler sampler;

    /* Options */
    int   planewidth[4], planeheight[4];
    int   inplanewidth[4], inplaneheight[4];
    int   in, out;
    int   width, height;
    float h_fov, v_fov;
    float ih_fov, iv_fov;
    float yaw, pitch, roll;
    char *rorder;
    int   rotation_order[3];
} V360VulkanContext;

/* Push constants */
struct PushData {
    float rot_mat[4][4];
    int in_img_size[4][2];
    float iflat_range[2];
    float flat_range[2];
};

static int get_rorder(char c)
{
    switch (c) {
    case 'Y':
    case 'y':
        return YAW;
    case 'P':
    case 'p':
        return PITCH;
    case 'R':
    case 'r':
        return ROLL;
    default:
        return -1;
    }
}

static av_cold int init_filter(AVFilterContext *ctx, AVFrame *in)
{
    int err;
    V360VulkanContext *s = ctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);

    for (int order = 0; order < NB_RORDERS; order++) {
        const char c = s->rorder[order];
        int rorder;

        if (c == '\0') {
            av_log(ctx, AV_LOG_WARNING,
                   "Incomplete rorder option. "
                   "Direction for all 3 rotation orders should be specified. "
                   "Switching to default rorder.\n");
            s->rotation_order[0] = YAW;
            s->rotation_order[1] = PITCH;
            s->rotation_order[2] = ROLL;
            break;
        }

        rorder = get_rorder(c);
        if (rorder == -1) {
            av_log(ctx, AV_LOG_WARNING,
                   "Incorrect rotation order symbol '%c' in rorder option. "
                   "Switching to default rorder.\n", c);
            s->rotation_order[0] = YAW;
            s->rotation_order[1] = PITCH;
            s->rotation_order[2] = ROLL;
            break;        }

        s->rotation_order[order] = rorder;
    }

    RET(ff_vk_init_sampler(vkctx, &s->sampler, 0, VK_FILTER_LINEAR));

    s->qf = ff_vk_qf_find(vkctx, VK_QUEUE_COMPUTE_BIT, 0);
    if (!s->qf) {
        av_log(ctx, AV_LOG_ERROR, "Device has no compute queues\n");
        err = AVERROR(ENOTSUP);
        goto fail;
    }

    RET(ff_vk_exec_pool_init(vkctx, s->qf, &s->e, 2, 0, 0, 0, NULL));

    SPEC_LIST_CREATE(sl, 4, 2*sizeof(int) + 2*sizeof(float))
    SPEC_LIST_ADD(sl, 0, 32, s->out);
    SPEC_LIST_ADD(sl, 1, 32, s->in);

    const float m_pi = M_PI, m_pi2 = M_PI_2;
    SPEC_LIST_ADD(sl, 2, 32, av_float2int(m_pi));
    SPEC_LIST_ADD(sl, 3, 32, av_float2int(m_pi2));

    ff_vk_shader_load(&s->shd, VK_SHADER_STAGE_COMPUTE_BIT,
                      sl, (uint32_t []) { 16, 16, 1 }, 0);

    ff_vk_shader_add_push_const(&s->shd, 0, sizeof(struct PushData),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    const FFVulkanDescriptorSetBinding desc_set[] = {
        { /* input_img */
            .type     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stages   = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems    = planes,
            .samplers = DUP_SAMPLER(s->sampler),
        },
        { /* output_img */
            .type   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stages = VK_SHADER_STAGE_COMPUTE_BIT,
            .elems  = planes,
        },
    };
    ff_vk_shader_add_descriptor_set(vkctx, &s->shd, desc_set, 2, 0, 0);

    RET(ff_vk_shader_link(vkctx, &s->shd,
                          ff_v360_comp_spv_data,
                          ff_v360_comp_spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, &s->e, &s->shd));

    s->initialized = 1;

fail:
    return err;
}

static void multiply_matrix(float c[4][4], const float a[4][4], const float b[4][4])
{
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            float sum = 0.0f;
            for (int k = 0; k < 3; k++)
                sum += a[i][k] * b[k][j];
            c[i][j] = sum;
        }
    }
}

static inline void calculate_iflat_range(int in, float ih_fov, float iv_fov,
                                         float *iflat_range)
{
    switch (in) {
    case FLAT:
        iflat_range[0] = tanf(0.5f * ih_fov * M_PI / 180.f);
        iflat_range[1] = tanf(0.5f * iv_fov * M_PI / 180.f);
        break;
    case STEREOGRAPHIC:
        iflat_range[0] = tanf(FFMIN(ih_fov, 359.f) * M_PI / 720.f);
        iflat_range[1] = tanf(FFMIN(iv_fov, 359.f) * M_PI / 720.f);
        break;
    case DUAL_FISHEYE:
    case FISHEYE:
        iflat_range[0] = ih_fov / 180.f;
        iflat_range[1] = iv_fov / 180.f;
        break;
    default:
        break;
    }
}

static inline void calculate_flat_range(int out, float h_fov, float v_fov,
                                        float *flat_range)
{
    switch (out) {
    case FLAT:
        flat_range[0] = tanf(0.5f * h_fov * M_PI / 180.f);
        flat_range[1] = tanf(0.5f * v_fov * M_PI / 180.f);
        break;
    case STEREOGRAPHIC:
        flat_range[0] = tanf(FFMIN(h_fov, 359.f) * M_PI / 720.f);
        flat_range[1] = tanf(FFMIN(v_fov, 359.f) * M_PI / 720.f);
        break;
    case DUAL_FISHEYE:
    case FISHEYE:
        flat_range[0] = h_fov / 180.f;
        flat_range[1] = v_fov / 180.f;
        break;
    default:
        break;
    }
}

static inline void calculate_rotation_matrix(float yaw, float pitch, float roll,
                                             float rot_mat[4][4],
                                             const int rotation_order[3])
{
    const float yaw_rad   = yaw   * M_PI / 180.f;
    const float pitch_rad = pitch * M_PI / 180.f;
    const float roll_rad  = roll  * M_PI / 180.f;

    const float sin_yaw   = sinf(yaw_rad);
    const float cos_yaw   = cosf(yaw_rad);
    const float sin_pitch = sinf(pitch_rad);
    const float cos_pitch = cosf(pitch_rad);
    const float sin_roll  = sinf(roll_rad);
    const float cos_roll  = cosf(roll_rad);

    float m[3][4][4];
    float temp[4][4];

    m[0][0][0] =  cos_yaw;  m[0][0][1] = 0;          m[0][0][2] =  sin_yaw;
    m[0][1][0] =  0;        m[0][1][1] = 1;          m[0][1][2] =  0;
    m[0][2][0] = -sin_yaw;  m[0][2][1] = 0;          m[0][2][2] =  cos_yaw;

    m[1][0][0] = 1;         m[1][0][1] = 0;          m[1][0][2] =  0;
    m[1][1][0] = 0;         m[1][1][1] = cos_pitch;  m[1][1][2] = -sin_pitch;
    m[1][2][0] = 0;         m[1][2][1] = sin_pitch;  m[1][2][2] =  cos_pitch;

    m[2][0][0] = cos_roll;  m[2][0][1] = -sin_roll;  m[2][0][2] =  0;
    m[2][1][0] = sin_roll;  m[2][1][1] =  cos_roll;  m[2][1][2] =  0;
    m[2][2][0] = 0;         m[2][2][1] =  0;         m[2][2][2] =  1;

    multiply_matrix(temp, m[rotation_order[0]], m[rotation_order[1]]);
    multiply_matrix(rot_mat, temp, m[rotation_order[2]]);
}

static int v360_vulkan_filter_frame(AVFilterLink *link, AVFrame *in)
{
    int err;
    AVFrame *out = NULL;
    AVFilterContext *ctx = link->dst;
    V360VulkanContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    if (!s->initialized)
        RET(init_filter(ctx, in));

    /* Push constants */
    struct PushData pd = { 0 };

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(s->vkctx.input_format);
    pd.in_img_size[0][0] = pd.in_img_size[3][0] = in->width;
    pd.in_img_size[0][1] = pd.in_img_size[3][1] = in->height;
    pd.in_img_size[1][0] = pd.in_img_size[2][0] =
        FF_CEIL_RSHIFT(in->width, desc->log2_chroma_w);
    pd.in_img_size[1][1] = pd.in_img_size[2][1] =
        FF_CEIL_RSHIFT(in->height, desc->log2_chroma_h);

    calculate_iflat_range(s->in, s->ih_fov, s->iv_fov, pd.iflat_range);
    calculate_flat_range(s->out, s->h_fov, s->v_fov, pd.flat_range);
    calculate_rotation_matrix(s->yaw, s->pitch, s->roll,
                              pd.rot_mat, s->rotation_order);

    RET(ff_vk_filter_process_simple(&s->vkctx, &s->e, &s->shd,
                                    out, in, s->sampler,
                                    &pd, sizeof(pd)));

    err = av_frame_copy_props(out, in);
    if (err < 0)
        goto fail;

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return err;
}

static void v360_vulkan_uninit(AVFilterContext *avctx)
{
    V360VulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;

    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_shader_free(vkctx, &s->shd);

    if (s->sampler)
        vk->DestroySampler(vkctx->hwctx->act_dev, s->sampler,
                           vkctx->hwctx->alloc);

    ff_vk_uninit(&s->vkctx);

    s->initialized = 0;
}

#define OFFSET(x) offsetof(V360VulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
#define DYNAMIC (FLAGS | AV_OPT_FLAG_RUNTIME_PARAM)
static const AVOption v360_vulkan_options[] = {
    {     "input", "set input projection",                OFFSET(in), AV_OPT_TYPE_INT,    {.i64=EQUIRECTANGULAR}, 0,    NB_PROJECTIONS-1,   FLAGS, "in" },
    {         "e", "equirectangular",                              0, AV_OPT_TYPE_CONST,  {.i64=EQUIRECTANGULAR}, 0,                   0,   FLAGS, "in" },
    {  "equirect", "equirectangular",                              0, AV_OPT_TYPE_CONST,  {.i64=EQUIRECTANGULAR}, 0,                   0,   FLAGS, "in" },
    {      "flat", "regular video",                                0, AV_OPT_TYPE_CONST,  {.i64=FLAT},            0,                   0,   FLAGS, "in" },
    {  "dfisheye", "dual fisheye",                                 0, AV_OPT_TYPE_CONST,  {.i64=DUAL_FISHEYE},    0,                   0,   FLAGS, "in" },
    {        "sg", "stereographic",                                0, AV_OPT_TYPE_CONST,  {.i64=STEREOGRAPHIC},   0,                   0,   FLAGS, "in" },
    {   "fisheye", "fisheye",                                      0, AV_OPT_TYPE_CONST,  {.i64=FISHEYE},         0,                   0,   FLAGS, "in" },

    {    "output", "set output projection",              OFFSET(out), AV_OPT_TYPE_INT,    {.i64=FLAT},            0,    NB_PROJECTIONS-1,   FLAGS, "out" },
    {         "e", "equirectangular",                              0, AV_OPT_TYPE_CONST,  {.i64=EQUIRECTANGULAR}, 0,                   0,   FLAGS, "out" },
    {  "equirect", "equirectangular",                              0, AV_OPT_TYPE_CONST,  {.i64=EQUIRECTANGULAR}, 0,                   0,   FLAGS, "out" },
    {      "flat", "regular video",                                0, AV_OPT_TYPE_CONST,  {.i64=FLAT},            0,                   0,   FLAGS, "out" },
    {  "dfisheye", "dual fisheye",                                 0, AV_OPT_TYPE_CONST,  {.i64=DUAL_FISHEYE},    0,                   0,   FLAGS, "out" },
    {        "sg", "stereographic",                                0, AV_OPT_TYPE_CONST,  {.i64=STEREOGRAPHIC},   0,                   0,   FLAGS, "out" },
    {   "fisheye", "fisheye",                                      0, AV_OPT_TYPE_CONST,  {.i64=FISHEYE},         0,                   0,   FLAGS, "out" },

    {         "w", "output width",                     OFFSET(width), AV_OPT_TYPE_INT,    {.i64 = 0},             0,           INT16_MAX,   FLAGS, "w" },
    {         "h", "output height",                   OFFSET(height), AV_OPT_TYPE_INT,    {.i64 = 0},             0,           INT16_MAX,   FLAGS, "h" },
    {       "yaw", "yaw rotation",                       OFFSET(yaw), AV_OPT_TYPE_FLOAT,  {.dbl = 0.0f},     -180.f,               180.f, DYNAMIC, "yaw" },
    {     "pitch", "pitch rotation",                   OFFSET(pitch), AV_OPT_TYPE_FLOAT,  {.dbl = 0.0f},     -180.f,               180.f, DYNAMIC, "pitch" },
    {      "roll", "roll rotation",                     OFFSET(roll), AV_OPT_TYPE_FLOAT,  {.dbl = 0.0f},     -180.f,               180.f, DYNAMIC, "roll" },
    {    "rorder", "rotation order",                  OFFSET(rorder), AV_OPT_TYPE_STRING, {.str = "ypr"},         0,                   0,   FLAGS, "rorder" },
    {     "h_fov", "set output horizontal FOV angle",  OFFSET(h_fov), AV_OPT_TYPE_FLOAT,  {.dbl = 90.0f},  0.00001f,              360.0f, DYNAMIC, "h_fov" },
    {     "v_fov", "set output vertical FOV angle",    OFFSET(v_fov), AV_OPT_TYPE_FLOAT,  {.dbl = 45.0f},  0.00001f,              360.0f, DYNAMIC, "v_fov" },
    {    "ih_fov", "set input horizontal FOV angle",  OFFSET(ih_fov), AV_OPT_TYPE_FLOAT,  {.dbl = 90.0f},  0.00001f,              360.0f, DYNAMIC, "ih_fov" },
    {    "iv_fov", "set input vertical FOV angle",    OFFSET(iv_fov), AV_OPT_TYPE_FLOAT,  {.dbl = 45.0f},  0.00001f,              360.0f, DYNAMIC, "iv_fov" },

    { NULL },
};

AVFILTER_DEFINE_CLASS(v360_vulkan);

static const AVFilterPad v360_vulkan_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &v360_vulkan_filter_frame,
        .config_props = &ff_vk_filter_config_input,
    },
};

static const AVFilterPad v360_vulkan_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vk_filter_config_output,
    },
};

const FFFilter ff_vf_v360_vulkan = {
    .p.name         = "v360_vulkan",
    .p.description  = NULL_IF_CONFIG_SMALL("Convert 360 projection of video."),
    .p.priv_class   = &v360_vulkan_class,
    .p.flags        = AVFILTER_FLAG_HWDEVICE,
    .priv_size      = sizeof(V360VulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &v360_vulkan_uninit,
    FILTER_INPUTS(v360_vulkan_inputs),
    FILTER_OUTPUTS(v360_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
