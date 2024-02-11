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
#include "libavutil/random_seed.h"
#include "libavutil/opt.h"
#include "vulkan_filter.h"
#include "vulkan_spirv.h"
#include "filters.h"
#include "internal.h"
#include "video.h"

#define IN_A  0
#define IN_B  1
#define IN_NB 2

typedef struct XFadeParameters {
    float progress;
} XFadeParameters;

typedef struct XFadeVulkanContext {
    FFVulkanContext     vkctx;

    int                 transition;
    int64_t             duration;
    int64_t             offset;

    int                 initialized;
    FFVulkanPipeline    pl;
    FFVkExecPool        e;
    FFVkQueueFamilyCtx  qf;
    FFVkSPIRVShader     shd;
    VkSampler           sampler;

    // PTS when the fade should start (in IN_A timebase)
    int64_t             start_pts;

    // PTS offset between IN_A and IN_B
    int64_t             inputs_offset_pts;

    // Duration of the transition
    int64_t             duration_pts;

    // Current PTS of the first input (IN_A)
    int64_t             pts;

    // If frames are currently just passed through
    // unmodified, like before and after the actual
    // transition.
    int                 passthrough;

    int                 status[IN_NB];
} XFadeVulkanContext;

enum XFadeTransitions {
    FADE,
    WIPELEFT,
    WIPERIGHT,
    WIPEUP,
    WIPEDOWN,
    SLIDEDOWN,
    SLIDEUP,
    SLIDELEFT,
    SLIDERIGHT,
    CIRCLEOPEN,
    CIRCLECLOSE,
    DISSOLVE,
    PIXELIZE,
    WIPETL,
    WIPETR,
    WIPEBL,
    WIPEBR,
    NB_TRANSITIONS,
};

static const char transition_fade[] = {
    C(0, void transition(int idx, ivec2 pos, float progress)                   )
    C(0, {                                                                     )
    C(1,     vec4 a = texture(a_images[idx], pos);                             )
    C(1,     vec4 b = texture(b_images[idx], pos);                             )
    C(1,     imageStore(output_images[idx], pos, mix(a, b, progress));         )
    C(0, }                                                                     )
};

static const char transition_wipeleft[] = {
    C(0, void transition(int idx, ivec2 pos, float progress)                   )
    C(0, {                                                                     )
    C(1,     ivec2 size = imageSize(output_images[idx]);                       )
    C(1,     int  s = int(size.x * (1.0 - progress));                          )
    C(1,     vec4 a = texture(a_images[idx], pos);                             )
    C(1,     vec4 b = texture(b_images[idx], pos);                             )
    C(1,     imageStore(output_images[idx], pos, pos.x > s ? b : a);           )
    C(0, }                                                                     )
};

static const char transition_wiperight[] = {
    C(0, void transition(int idx, ivec2 pos, float progress)                   )
    C(0, {                                                                     )
    C(1,     ivec2 size = imageSize(output_images[idx]);                       )
    C(1,     int  s = int(size.x * progress);                                  )
    C(1,     vec4 a = texture(a_images[idx], pos);                             )
    C(1,     vec4 b = texture(b_images[idx], pos);                             )
    C(1,     imageStore(output_images[idx], pos, pos.x > s ? a : b);           )
    C(0, }                                                                     )
};

static const char transition_wipeup[] = {
    C(0, void transition(int idx, ivec2 pos, float progress)                   )
    C(0, {                                                                     )
    C(1,     ivec2 size = imageSize(output_images[idx]);                       )
    C(1,     int  s = int(size.y * (1.0 - progress));                          )
    C(1,     vec4 a = texture(a_images[idx], pos);                             )
    C(1,     vec4 b = texture(b_images[idx], pos);                             )
    C(1,     imageStore(output_images[idx], pos, pos.y > s ? b : a);           )
    C(0, }                                                                     )
};

static const char transition_wipedown[] = {
    C(0, void transition(int idx, ivec2 pos, float progress)                   )
    C(0, {                                                                     )
    C(1,     ivec2 size = imageSize(output_images[idx]);                       )
    C(1,     int  s = int(size.y * progress);                                  )
    C(1,     vec4 a = texture(a_images[idx], pos);                             )
    C(1,     vec4 b = texture(b_images[idx], pos);                             )
    C(1,     imageStore(output_images[idx], pos, pos.y > s ? a : b);           )
    C(0, }                                                                     )
};

#define SHADER_SLIDE_COMMON                                                              \
    C(0, void slide(int idx, ivec2 pos, float progress, ivec2 direction)               ) \
    C(0, {                                                                             ) \
    C(1,     ivec2 size = imageSize(output_images[idx]);                               ) \
    C(1,     ivec2 pi = ivec2(progress * size);                                        ) \
    C(1,     ivec2 p = pos + pi * direction;                                           ) \
    C(1,     ivec2 f = p % size;                                                       ) \
    C(1,     f = f + size * ivec2(f.x < 0, f.y < 0);                                   ) \
    C(1,     vec4 a = texture(a_images[idx], f);                                       ) \
    C(1,     vec4 b = texture(b_images[idx], f);                                       ) \
    C(1,     vec4 r = (p.y >= 0 && p.x >= 0 && size.y > p.y &&  size.x > p.x) ? a : b; ) \
    C(1,     imageStore(output_images[idx], pos, r);                                   ) \
    C(0, }                                                                             )

static const char transition_slidedown[] = {
    SHADER_SLIDE_COMMON
    C(0, void transition(int idx, ivec2 pos, float progress)                   )
    C(0, {                                                                     )
    C(1,     slide(idx, pos, progress, ivec2(0, -1));                          )
    C(0, }                                                                     )
};

static const char transition_slideup[] = {
    SHADER_SLIDE_COMMON
    C(0, void transition(int idx, ivec2 pos, float progress)                   )
    C(0, {                                                                     )
    C(1,     slide(idx, pos, progress, ivec2(0, +1));                          )
    C(0, }                                                                     )
};

static const char transition_slideleft[] = {
    SHADER_SLIDE_COMMON
    C(0, void transition(int idx, ivec2 pos, float progress)                   )
    C(0, {                                                                     )
    C(1,     slide(idx, pos, progress, ivec2(+1, 0));                          )
    C(0, }                                                                     )
};

static const char transition_slideright[] = {
    SHADER_SLIDE_COMMON
    C(0, void transition(int idx, ivec2 pos, float progress)                   )
    C(0, {                                                                     )
    C(1,     slide(idx, pos, progress, ivec2(-1, 0));                          )
    C(0, }                                                                     )
};

#define SHADER_CIRCLE_COMMON                                                     \
    C(0, void circle(int idx, ivec2 pos, float progress, bool open)            ) \
    C(0, {                                                                     ) \
    C(1,     const ivec2 half_size = imageSize(output_images[idx]) / 2;        ) \
    C(1,     const float z = dot(half_size, half_size);                        ) \
    C(1,     float p = ((open ? (1.0 - progress) : progress) - 0.5) * 3.0;     ) \
    C(1,     ivec2 dsize = pos - half_size;                                    ) \
    C(1,     float sm = dot(dsize, dsize) / z + p;                             ) \
    C(1,     vec4 a = texture(a_images[idx], pos);                             ) \
    C(1,     vec4 b = texture(b_images[idx], pos);                             ) \
    C(1,     imageStore(output_images[idx], pos, \
                        mix(open ? b : a, open ? a : b, \
                            smoothstep(0.f, 1.f, sm)));                        ) \
    C(0, }                                                                     )

static const char transition_circleopen[] = {
    SHADER_CIRCLE_COMMON
    C(0, void transition(int idx, ivec2 pos, float progress)                   )
    C(0, {                                                                     )
    C(1,     circle(idx, pos, progress, true);                                 )
    C(0, }                                                                     )
};

static const char transition_circleclose[] = {
    SHADER_CIRCLE_COMMON
    C(0, void transition(int idx, ivec2 pos, float progress)                   )
    C(0, {                                                                     )
    C(1,     circle(idx, pos, progress, false);                                )
    C(0, }                                                                     )
};

#define SHADER_FRAND_FUNC                                                        \
    C(0, float frand(vec2 v)                                                   ) \
    C(0, {                                                                     ) \
    C(1,     return fract(sin(dot(v, vec2(12.9898, 78.233))) * 43758.545);     ) \
    C(0, }                                                                     )

static const char transition_dissolve[] = {
    SHADER_FRAND_FUNC
    C(0, void transition(int idx, ivec2 pos, float progress)                   )
    C(0, {                                                                     )
    C(1,     float sm = frand(pos) * 2.0 + (1.0 - progress) * 2.0 - 1.5;       )
    C(1,     vec4 a = texture(a_images[idx], pos);                             )
    C(1,     vec4 b = texture(b_images[idx], pos);                             )
    C(1,     imageStore(output_images[idx], pos, sm >= 0.5 ? a : b);           )
    C(0, }                                                                     )
};

static const char transition_pixelize[] = {
    C(0, void transition(int idx, ivec2 pos, float progress)                                  )
    C(0, {                                                                                    )
    C(1,     ivec2 size = imageSize(output_images[idx]);                                      )
    C(1,     float d = min(progress, 1.0 - progress);                                         )
    C(1,     float dist = ceil(d * 50.0) / 50.0;                                              )
    C(1,     float sq = 2.0 * dist * min(size.x, size.y) / 20.0;                              )
    C(1,     float sx = dist > 0.0 ? min((floor(pos.x / sq) + 0.5) * sq, size.x - 1) : pos.x; )
    C(1,     float sy = dist > 0.0 ? min((floor(pos.y / sq) + 0.5) * sq, size.y - 1) : pos.y; )
    C(1,     vec4 a = texture(a_images[idx], vec2(sx, sy));                                   )
    C(1,     vec4 b = texture(b_images[idx], vec2(sx, sy));                                   )
    C(1,     imageStore(output_images[idx], pos, mix(a, b, progress));                        )
    C(0, }                                                                                    )
};

static const char transition_wipetl[] = {
    C(0, void transition(int idx, ivec2 pos, float progress)                                  )
    C(0, {                                                                                    )
    C(1,     ivec2 size = imageSize(output_images[idx]);                                      )
    C(1,     float zw = size.x * (1.0 - progress);                                            )
    C(1,     float zh = size.y * (1.0 - progress);                                            )
    C(1,     vec4 a = texture(a_images[idx], pos);                                            )
    C(1,     vec4 b = texture(b_images[idx], pos);                                            )
    C(1,     imageStore(output_images[idx], pos, (pos.y <= zh && pos.x <= zw) ? a : b);       )
    C(0, }                                                                                    )
};

static const char transition_wipetr[] = {
    C(0, void transition(int idx, ivec2 pos, float progress)                                  )
    C(0, {                                                                                    )
    C(1,     ivec2 size = imageSize(output_images[idx]);                                      )
    C(1,     float zw = size.x * (progress);                                                  )
    C(1,     float zh = size.y * (1.0 - progress);                                            )
    C(1,     vec4 a = texture(a_images[idx], pos);                                            )
    C(1,     vec4 b = texture(b_images[idx], pos);                                            )
    C(1,     imageStore(output_images[idx], pos, (pos.y <= zh && pos.x > zw) ? a : b);        )
    C(0, }                                                                                    )
};

static const char transition_wipebl[] = {
    C(0, void transition(int idx, ivec2 pos, float progress)                                  )
    C(0, {                                                                                    )
    C(1,     ivec2 size = imageSize(output_images[idx]);                                      )
    C(1,     float zw = size.x * (1.0 - progress);                                            )
    C(1,     float zh = size.y * (progress);                                                  )
    C(1,     vec4 a = texture(a_images[idx], pos);                                            )
    C(1,     vec4 b = texture(b_images[idx], pos);                                            )
    C(1,     imageStore(output_images[idx], pos, (pos.y > zh && pos.x <= zw) ? a : b);        )
    C(0, }                                                                                    )
};

static const char transition_wipebr[] = {
    C(0, void transition(int idx, ivec2 pos, float progress)                                  )
    C(0, {                                                                                    )
    C(1,     ivec2 size = imageSize(output_images[idx]);                                      )
    C(1,     float zw = size.x * (progress);                                                  )
    C(1,     float zh = size.y * (progress);                                                  )
    C(1,     vec4 a = texture(a_images[idx], pos);                                            )
    C(1,     vec4 b = texture(b_images[idx], pos);                                            )
    C(1,     imageStore(output_images[idx], pos, (pos.y > zh && pos.x > zw) ? a : b);         )
    C(0, }                                                                                    )
};

static const char* transitions_map[NB_TRANSITIONS] = {
    [FADE]          = transition_fade,
    [WIPELEFT]      = transition_wipeleft,
    [WIPERIGHT]     = transition_wiperight,
    [WIPEUP]        = transition_wipeup,
    [WIPEDOWN]      = transition_wipedown,
    [SLIDEDOWN]     = transition_slidedown,
    [SLIDEUP]       = transition_slideup,
    [SLIDELEFT]     = transition_slideleft,
    [SLIDERIGHT]    = transition_slideright,
    [CIRCLEOPEN]    = transition_circleopen,
    [CIRCLECLOSE]   = transition_circleclose,
    [DISSOLVE]      = transition_dissolve,
    [PIXELIZE]      = transition_pixelize,
    [WIPETL]        = transition_wipetl,
    [WIPETR]        = transition_wipetr,
    [WIPEBL]        = transition_wipebl,
    [WIPEBR]        = transition_wipebr,
};

static av_cold int init_vulkan(AVFilterContext *avctx)
{
    int err = 0;
    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    XFadeVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);
    FFVkSPIRVShader *shd = &s->shd;
    FFVkSPIRVCompiler *spv;
    FFVulkanDescriptorSetBinding *desc;

    spv = ff_vk_spirv_init();
    if (!spv) {
        av_log(avctx, AV_LOG_ERROR, "Unable to initialize SPIR-V compiler!\n");
        return AVERROR_EXTERNAL;
    }

    ff_vk_qf_init(vkctx, &s->qf, VK_QUEUE_COMPUTE_BIT);
    RET(ff_vk_exec_pool_init(vkctx, &s->qf, &s->e, s->qf.nb_queues*4, 0, 0, 0, NULL));
    RET(ff_vk_init_sampler(vkctx, &s->sampler, 1, VK_FILTER_NEAREST));
    RET(ff_vk_shader_init(&s->pl, &s->shd, "xfade_compute",
                          VK_SHADER_STAGE_COMPUTE_BIT, 0));

    ff_vk_shader_set_compute_sizes(&s->shd, 32, 32, 1);

    desc = (FFVulkanDescriptorSetBinding []) {
        {
            .name       = "a_images",
            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .samplers   = DUP_SAMPLER(s->sampler),
        },
        {
            .name       = "b_images",
            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .samplers   = DUP_SAMPLER(s->sampler),
        },
        {
            .name       = "output_images",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .mem_layout = ff_vk_shader_rep_fmt(s->vkctx.output_format),
            .mem_quali  = "writeonly",
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };

    RET(ff_vk_pipeline_descriptor_set_add(vkctx, &s->pl, shd, desc, 3, 0, 0));

    GLSLC(0, layout(push_constant, std430) uniform pushConstants {                 );
    GLSLC(1,    float progress;                                                    );
    GLSLC(0, };                                                                    );

    ff_vk_add_push_constant(&s->pl, 0, sizeof(XFadeParameters),
                            VK_SHADER_STAGE_COMPUTE_BIT);

    // Add the right transition type function to the shader
    GLSLD(transitions_map[s->transition]);

    GLSLC(0, void main()                                                  );
    GLSLC(0, {                                                            );
    GLSLC(1,     ivec2 pos = ivec2(gl_GlobalInvocationID.xy);             );
    GLSLF(1,     int planes = %i;                                  ,planes);
    GLSLC(1,     for (int i = 0; i < planes; i++) {                       );
    GLSLC(2,        transition(i, pos, progress);                         );
    GLSLC(1,     }                                                        );
    GLSLC(0, }                                                            );

    RET(spv->compile_shader(spv, avctx, shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_create(vkctx, shd, spv_data, spv_len, "main"));

    RET(ff_vk_init_compute_pipeline(vkctx, &s->pl, shd));
    RET(ff_vk_exec_pipeline_register(vkctx, &s->e, &s->pl));

    s->initialized = 1;

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);
    if (spv)
        spv->uninit(&spv);

    return err;
}

static int xfade_frame(AVFilterContext *avctx, AVFrame *frame_a, AVFrame *frame_b)
{
    int err;
    AVFilterLink *outlink = avctx->outputs[0];
    XFadeVulkanContext *s = avctx->priv;
    float progress;

    AVFrame *output = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!output) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    if (!s->initialized) {
        AVHWFramesContext *a_fc = (AVHWFramesContext*)frame_a->hw_frames_ctx->data;
        AVHWFramesContext *b_fc = (AVHWFramesContext*)frame_b->hw_frames_ctx->data;
        if (a_fc->sw_format != b_fc->sw_format) {
            av_log(avctx, AV_LOG_ERROR,
                   "Currently the sw format of the first input needs to match the second!\n");
            return AVERROR(EINVAL);
        }
        RET(init_vulkan(avctx));
    }

    RET(av_frame_copy_props(output, frame_a));
    output->pts = s->pts;

    progress = av_clipf((float)(s->pts - s->start_pts) / s->duration_pts,
                        0.f, 1.f);

    RET(ff_vk_filter_process_Nin(&s->vkctx, &s->e, &s->pl, output,
                                 (AVFrame *[]){ frame_a, frame_b }, 2, s->sampler,
                                 &(XFadeParameters){ progress }, sizeof(XFadeParameters)));

    return ff_filter_frame(outlink, output);

fail:
    av_frame_free(&output);
    return err;
}

static int config_props_output(AVFilterLink *outlink)
{
    int err;
    AVFilterContext *avctx = outlink->src;
    XFadeVulkanContext *s = avctx->priv;
    AVFilterLink *inlink_a = avctx->inputs[IN_A];
    AVFilterLink *inlink_b = avctx->inputs[IN_B];

    if (inlink_a->w != inlink_b->w || inlink_a->h != inlink_b->h) {
        av_log(avctx, AV_LOG_ERROR, "First input link %s parameters "
               "(size %dx%d) do not match the corresponding "
               "second input link %s parameters (size %dx%d)\n",
               avctx->input_pads[IN_A].name, inlink_a->w, inlink_a->h,
               avctx->input_pads[IN_B].name, inlink_b->w, inlink_b->h);
        return AVERROR(EINVAL);
    }

    if (inlink_a->time_base.num != inlink_b->time_base.num ||
        inlink_a->time_base.den != inlink_b->time_base.den) {
        av_log(avctx, AV_LOG_ERROR, "First input link %s timebase "
               "(%d/%d) does not match the corresponding "
               "second input link %s timebase (%d/%d)\n",
               avctx->input_pads[IN_A].name, inlink_a->time_base.num, inlink_a->time_base.den,
               avctx->input_pads[IN_B].name, inlink_b->time_base.num, inlink_b->time_base.den);
        return AVERROR(EINVAL);
    }

    s->start_pts = s->inputs_offset_pts = AV_NOPTS_VALUE;

    outlink->time_base = inlink_a->time_base;
    outlink->frame_rate = inlink_a->frame_rate;
    outlink->sample_aspect_ratio = inlink_a->sample_aspect_ratio;

    if (s->duration)
        s->duration_pts = av_rescale_q(s->duration, AV_TIME_BASE_Q, inlink_a->time_base);
    RET(ff_vk_filter_config_output(outlink));

fail:
    return err;
}

static int forward_frame(XFadeVulkanContext *s,
                         AVFilterLink *inlink, AVFilterLink *outlink)
{
    int64_t status_pts;
    int ret = 0, status;
    AVFrame *frame = NULL;

    ret = ff_inlink_consume_frame(inlink, &frame);
    if (ret < 0)
        return ret;

    if (ret > 0) {
        // If we do not have an offset yet, it's because we
        // never got a first input. Just offset to 0
        if (s->inputs_offset_pts == AV_NOPTS_VALUE)
            s->inputs_offset_pts = -frame->pts;

        // We got a frame, nothing to do other than adjusting the timestamp
        frame->pts += s->inputs_offset_pts;
        return ff_filter_frame(outlink, frame);
    }

    // Forward status with our timestamp
    if (ff_inlink_acknowledge_status(inlink, &status, &status_pts)) {
        if (s->inputs_offset_pts == AV_NOPTS_VALUE)
            s->inputs_offset_pts = -status_pts;

        ff_outlink_set_status(outlink, status, status_pts + s->inputs_offset_pts);
        return 0;
    }

    // No frame available, request one if needed
    if (ff_outlink_frame_wanted(outlink))
        ff_inlink_request_frame(inlink);

    return 0;
}

static int activate(AVFilterContext *avctx)
{
    XFadeVulkanContext *s = avctx->priv;
    AVFilterLink *in_a = avctx->inputs[IN_A];
    AVFilterLink *in_b = avctx->inputs[IN_B];
    AVFilterLink *outlink = avctx->outputs[0];
    int64_t status_pts;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(outlink, avctx);

    // Check if we already transitioned or IN_A ended prematurely,
    // in which case just forward the frames from IN_B with adjusted
    // timestamps until EOF.
    if (s->status[IN_A] && !s->status[IN_B])
        return forward_frame(s, in_b, outlink);

    // We did not finish transitioning yet and the first stream
    // did not end either, so check if there are more frames to consume.
    if (ff_inlink_check_available_frame(in_a)) {
        AVFrame *peeked_frame = ff_inlink_peek_frame(in_a, 0);
        s->pts = peeked_frame->pts;

        if (s->start_pts == AV_NOPTS_VALUE)
            s->start_pts =
                s->pts + av_rescale_q(s->offset, AV_TIME_BASE_Q, in_a->time_base);

        // Check if we are not yet transitioning, in which case
        // just request and forward the input frame.
        if (s->start_pts > s->pts) {
            AVFrame *frame_a = NULL;
            s->passthrough = 1;
            ff_inlink_consume_frame(in_a, &frame_a);
            return ff_filter_frame(outlink, frame_a);
        }
        s->passthrough = 0;

        // We are transitioning, so we need a frame from IN_B
        if (ff_inlink_check_available_frame(in_b)) {
            int ret;
            AVFrame *frame_a = NULL, *frame_b = NULL;
            ff_inlink_consume_frame(avctx->inputs[IN_A], &frame_a);
            ff_inlink_consume_frame(avctx->inputs[IN_B], &frame_b);

            // Calculate PTS offset to first input
            if (s->inputs_offset_pts == AV_NOPTS_VALUE)
                s->inputs_offset_pts = s->pts - frame_b->pts;

            // Check if we finished transitioning, in which case we
            // report back EOF to IN_A as it is no longer needed.
            if (s->pts - s->start_pts > s->duration_pts) {
                s->status[IN_A] = AVERROR_EOF;
                ff_inlink_set_status(in_a, AVERROR_EOF);
                s->passthrough = 1;
            }
            ret = xfade_frame(avctx, frame_a, frame_b);
            av_frame_free(&frame_a);
            av_frame_free(&frame_b);
            return ret;
        }

        // We did not get a frame from IN_B, check its status.
        if (ff_inlink_acknowledge_status(in_b, &s->status[IN_B], &status_pts)) {
            // We should transition, but IN_B is EOF so just report EOF output now.
            ff_outlink_set_status(outlink, s->status[IN_B], s->pts);
            return 0;
        }

        // We did not get a frame for IN_B but no EOF either, so just request more.
        if (ff_outlink_frame_wanted(outlink)) {
            ff_inlink_request_frame(in_b);
            return 0;
        }
    }

    // We did not get a frame from IN_A, check its status.
    if (ff_inlink_acknowledge_status(in_a, &s->status[IN_A], &status_pts)) {
        // No more frames from IN_A, do not report EOF though, we will just
        // forward the IN_B frames in the next activate calls.
        s->passthrough = 1;
        ff_filter_set_ready(avctx, 100);
        return 0;
    }

    // We have no frames yet from IN_A and no EOF, so request some.
    if (ff_outlink_frame_wanted(outlink)) {
        ff_inlink_request_frame(in_a);
        return 0;
    }

    return FFERROR_NOT_READY;
}

static av_cold void uninit(AVFilterContext *avctx)
{
    XFadeVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;

    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_pipeline_free(vkctx, &s->pl);
    ff_vk_shader_free(vkctx, &s->shd);

    if (s->sampler)
        vk->DestroySampler(vkctx->hwctx->act_dev, s->sampler,
                           vkctx->hwctx->alloc);

    ff_vk_uninit(&s->vkctx);

    s->initialized = 0;
}

static AVFrame *get_video_buffer(AVFilterLink *inlink, int w, int h)
{
    XFadeVulkanContext *s = inlink->dst->priv;

    return s->passthrough ?
        ff_null_get_video_buffer   (inlink, w, h) :
        ff_default_get_video_buffer(inlink, w, h);
}

#define OFFSET(x) offsetof(XFadeVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption xfade_vulkan_options[] = {
    { "transition", "set cross fade transition", OFFSET(transition), AV_OPT_TYPE_INT, {.i64=FADE}, 0, NB_TRANSITIONS-1, FLAGS, .unit = "transition" },
        { "fade", "fade transition", 0, AV_OPT_TYPE_CONST, {.i64=FADE}, 0, 0, FLAGS, .unit = "transition" },
        { "wipeleft", "wipe left transition", 0, AV_OPT_TYPE_CONST, {.i64=WIPELEFT}, 0, 0, FLAGS, .unit = "transition" },
        { "wiperight", "wipe right transition", 0, AV_OPT_TYPE_CONST, {.i64=WIPERIGHT}, 0, 0, FLAGS, .unit = "transition" },
        { "wipeup", "wipe up transition", 0, AV_OPT_TYPE_CONST, {.i64=WIPEUP}, 0, 0, FLAGS, .unit = "transition" },
        { "wipedown", "wipe down transition", 0, AV_OPT_TYPE_CONST, {.i64=WIPEDOWN}, 0, 0, FLAGS, .unit = "transition" },
        { "slidedown", "slide down transition", 0, AV_OPT_TYPE_CONST, {.i64=SLIDEDOWN}, 0, 0, FLAGS, .unit = "transition" },
        { "slideup", "slide up transition", 0, AV_OPT_TYPE_CONST, {.i64=SLIDEUP}, 0, 0, FLAGS, .unit = "transition" },
        { "slideleft", "slide left transition", 0, AV_OPT_TYPE_CONST, {.i64=SLIDELEFT}, 0, 0, FLAGS, .unit = "transition" },
        { "slideright", "slide right transition", 0, AV_OPT_TYPE_CONST, {.i64=SLIDERIGHT}, 0, 0, FLAGS, .unit = "transition" },
        { "circleopen", "circleopen transition", 0, AV_OPT_TYPE_CONST, {.i64=CIRCLEOPEN}, 0, 0, FLAGS, .unit = "transition" },
        { "circleclose", "circleclose transition", 0, AV_OPT_TYPE_CONST, {.i64=CIRCLECLOSE}, 0, 0, FLAGS, .unit = "transition" },
        { "dissolve", "dissolve transition", 0, AV_OPT_TYPE_CONST, {.i64=DISSOLVE}, 0, 0, FLAGS, .unit = "transition" },
        { "pixelize", "pixelize transition", 0, AV_OPT_TYPE_CONST, {.i64=PIXELIZE}, 0, 0, FLAGS, .unit = "transition" },
        { "wipetl", "wipe top left transition", 0, AV_OPT_TYPE_CONST, {.i64=WIPETL}, 0, 0, FLAGS, .unit = "transition" },
        { "wipetr", "wipe top right transition", 0, AV_OPT_TYPE_CONST, {.i64=WIPETR}, 0, 0, FLAGS, .unit = "transition" },
        { "wipebl", "wipe bottom left transition", 0, AV_OPT_TYPE_CONST, {.i64=WIPEBL}, 0, 0, FLAGS, .unit = "transition" },
        { "wipebr", "wipe bottom right transition", 0, AV_OPT_TYPE_CONST, {.i64=WIPEBR}, 0, 0, FLAGS, .unit = "transition" },
    { "duration", "set cross fade duration", OFFSET(duration), AV_OPT_TYPE_DURATION, {.i64=1000000}, 0, 60000000, FLAGS },
    { "offset",   "set cross fade start relative to first input stream", OFFSET(offset), AV_OPT_TYPE_DURATION, {.i64=0}, INT64_MIN, INT64_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(xfade_vulkan);

static const AVFilterPad xfade_vulkan_inputs[] = {
    {
        .name             = "main",
        .type             = AVMEDIA_TYPE_VIDEO,
        .get_buffer.video = &get_video_buffer,
        .config_props     = &ff_vk_filter_config_input,
    },
    {
        .name             = "xfade",
        .type             = AVMEDIA_TYPE_VIDEO,
        .get_buffer.video = &get_video_buffer,
        .config_props     = &ff_vk_filter_config_input,
    },
};

static const AVFilterPad xfade_vulkan_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = &config_props_output,
    },
};

const AVFilter ff_vf_xfade_vulkan = {
    .name            = "xfade_vulkan",
    .description     = NULL_IF_CONFIG_SMALL("Cross fade one video with another video."),
    .priv_size       = sizeof(XFadeVulkanContext),
    .init            = &ff_vk_filter_init,
    .uninit          = &uninit,
    .activate        = &activate,
    FILTER_INPUTS(xfade_vulkan_inputs),
    FILTER_OUTPUTS(xfade_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .priv_class      = &xfade_vulkan_class,
    .flags_internal  = FF_FILTER_FLAG_HWFRAME_AWARE,
    .flags           = AVFILTER_FLAG_HWDEVICE,
};
