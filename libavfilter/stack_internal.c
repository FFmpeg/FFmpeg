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

#define OFFSET(x) offsetof(StackHWContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM)

#define SET_OUTPUT_REGION(region, rx, ry, rw, rh) do {  \
        region->x = rx;                                 \
        region->y = ry;                                 \
        region->width = rw;                             \
        region->height = rh;                            \
    } while (0)

static int init_framesync(AVFilterContext *avctx)
{
    StackBaseContext *sctx = avctx->priv;
    int ret;

    ret = ff_framesync_init(&sctx->fs, avctx, avctx->nb_inputs);
    if (ret < 0)
        return ret;

    sctx->fs.on_event = process_frame;
    sctx->fs.opaque = sctx;

    for (int i = 0; i < sctx->nb_inputs; i++) {
        FFFrameSyncIn *in = &sctx->fs.in[i];

        in->before = EXT_STOP;
        in->after = sctx->shortest ? EXT_STOP : EXT_INFINITY;
        in->sync = 1;
        in->time_base = avctx->inputs[i]->time_base;
    }

    return ff_framesync_configure(&sctx->fs);
}

static int config_comm_output(AVFilterLink *outlink)
{
    FilterLink *outl = ff_filter_link(outlink);
    AVFilterContext *avctx = outlink->src;
    StackBaseContext *sctx = avctx->priv;
    AVFilterLink *inlink0 = avctx->inputs[0];
    FilterLink *inl0 = ff_filter_link(inlink0);
    int width, height, ret;

    if (sctx->mode == STACK_H) {
        height = sctx->tile_height;
        width = 0;

        if (!height)
            height = inlink0->h;

        for (int i = 0; i < sctx->nb_inputs; i++) {
            AVFilterLink *inlink = avctx->inputs[i];
            StackItemRegion *region = &sctx->regions[i];

            SET_OUTPUT_REGION(region, width, 0, av_rescale(height, inlink->w, inlink->h), height);
            width += av_rescale(height, inlink->w, inlink->h);
        }
    } else if (sctx->mode == STACK_V) {
        height = 0;
        width = sctx->tile_width;

        if (!width)
            width = inlink0->w;

        for (int i = 0; i < sctx->nb_inputs; i++) {
            AVFilterLink *inlink = avctx->inputs[i];
            StackItemRegion *region = &sctx->regions[i];

            SET_OUTPUT_REGION(region, 0, height, width, av_rescale(width, inlink->h, inlink->w));
            height += av_rescale(width, inlink->h, inlink->w);
        }
    } else if (sctx->nb_grid_rows && sctx->nb_grid_columns) {
        int xpos = 0, ypos = 0;
        int ow, oh, k = 0;

        ow = sctx->tile_width;
        oh = sctx->tile_height;

        if (!ow || !oh) {
            ow = avctx->inputs[0]->w;
            oh = avctx->inputs[0]->h;
        }

        for (int i = 0; i < sctx->nb_grid_columns; i++) {
            ypos = 0;

            for (int j = 0; j < sctx->nb_grid_rows; j++) {
                StackItemRegion *region = &sctx->regions[k++];

                SET_OUTPUT_REGION(region, xpos, ypos, ow, oh);
                ypos += oh;
            }

            xpos += ow;
        }

        width = ow * sctx->nb_grid_columns;
        height = oh * sctx->nb_grid_rows;
    } else {
        char *arg, *p = sctx->layout, *saveptr = NULL;
        char *arg2, *p2, *saveptr2 = NULL;
        char *arg3, *p3, *saveptr3 = NULL;
        int xpos, ypos, size;
        int ow, oh;

        width = 0;
        height = 0;

        for (int i = 0; i < sctx->nb_inputs; i++) {
            AVFilterLink *inlink = avctx->inputs[i];
            StackItemRegion *region = &sctx->regions[i];

            ow = inlink->w;
            oh = inlink->h;

            if (!(arg = av_strtok(p, "|", &saveptr)))
                return AVERROR(EINVAL);

            p = NULL;
            p2 = arg;
            xpos = ypos = 0;

            for (int j = 0; j < 3; j++) {
                if (!(arg2 = av_strtok(p2, "_", &saveptr2))) {
                    if (j == 2)
                        break;
                    else
                        return AVERROR(EINVAL);
                }

                p2 = NULL;
                p3 = arg2;

                if (j == 2) {
                    if ((ret = av_parse_video_size(&ow, &oh, p3)) < 0) {
                        av_log(avctx, AV_LOG_ERROR, "Invalid size '%s'\n", p3);
                        return ret;
                    }

                    break;
                }

                while ((arg3 = av_strtok(p3, "+", &saveptr3))) {
                    p3 = NULL;
                    if (sscanf(arg3, "w%d", &size) == 1) {
                        if (size == i || size < 0 || size >= sctx->nb_inputs)
                            return AVERROR(EINVAL);

                        if (!j)
                            xpos += sctx->regions[size].width;
                        else
                            ypos += sctx->regions[size].width;
                    } else if (sscanf(arg3, "h%d", &size) == 1) {
                        if (size == i || size < 0 || size >= sctx->nb_inputs)
                            return AVERROR(EINVAL);

                        if (!j)
                            xpos += sctx->regions[size].height;
                        else
                            ypos += sctx->regions[size].height;
                    } else if (sscanf(arg3, "%d", &size) == 1) {
                        if (size < 0)
                            return AVERROR(EINVAL);

                        if (!j)
                            xpos += size;
                        else
                            ypos += size;
                    } else {
                        return AVERROR(EINVAL);
                    }
                }
            }

            SET_OUTPUT_REGION(region, xpos, ypos, ow, oh);
            width = FFMAX(width,  xpos + ow);
            height = FFMAX(height, ypos + oh);
        }

    }

    outlink->w = width;
    outlink->h = height;
    outl->frame_rate = inl0->frame_rate;
    outlink->sample_aspect_ratio = inlink0->sample_aspect_ratio;

    for (int i = 1; i < sctx->nb_inputs; i++) {
        FilterLink *inlink = ff_filter_link(avctx->inputs[i]);
        if (outl->frame_rate.num != inlink->frame_rate.num ||
            outl->frame_rate.den != inlink->frame_rate.den) {
            av_log(avctx, AV_LOG_VERBOSE,
                    "Video inputs have different frame rates, output will be VFR\n");
            outl->frame_rate = av_make_q(1, 0);
            break;
        }
    }

    ret = init_framesync(avctx);
    if (ret < 0)
        return ret;

    outlink->time_base = sctx->fs.time_base;

    return 0;
}

static int stack_init(AVFilterContext *avctx)
{
    StackBaseContext *sctx = avctx->priv;
    int ret;

    if (!strcmp(avctx->filter->name, HSTACK_NAME))
        sctx->mode = STACK_H;
    else if (!strcmp(avctx->filter->name, VSTACK_NAME))
        sctx->mode = STACK_V;
    else {
        int is_grid;

        av_assert0(strcmp(avctx->filter->name, XSTACK_NAME) == 0);
        sctx->mode = STACK_X;
        is_grid = sctx->nb_grid_rows && sctx->nb_grid_columns;

        if (sctx->layout && is_grid) {
            av_log(avctx, AV_LOG_ERROR, "Both layout and grid were specified. Only one is allowed.\n");
            return AVERROR(EINVAL);
        }

        if (!sctx->layout && !is_grid) {
            if (sctx->nb_inputs == 2) {
                sctx->nb_grid_rows = 1;
                sctx->nb_grid_columns = 2;
                is_grid = 1;
            } else {
                av_log(avctx, AV_LOG_ERROR, "No layout or grid specified.\n");
                return AVERROR(EINVAL);
            }
        }

        if (is_grid)
            sctx->nb_inputs = sctx->nb_grid_rows * sctx->nb_grid_columns;

        if (strcmp(sctx->fillcolor_str, "none") &&
            av_parse_color(sctx->fillcolor, sctx->fillcolor_str, -1, avctx) >= 0) {
            sctx->fillcolor_enable = 1;
        } else {
            sctx->fillcolor_enable = 0;
        }
    }

    for (int i = 0; i < sctx->nb_inputs; i++) {
        AVFilterPad pad = { 0 };

        pad.type = AVMEDIA_TYPE_VIDEO;
        pad.name = av_asprintf("input%d", i);

        if (!pad.name)
            return AVERROR(ENOMEM);

        if ((ret = ff_append_inpad_free_name(avctx, &pad)) < 0)
            return ret;
    }

    sctx->regions = av_calloc(sctx->nb_inputs, sizeof(*sctx->regions));
    if (!sctx->regions)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void stack_uninit(AVFilterContext *avctx)
{
    StackBaseContext *sctx = avctx->priv;

    av_freep(&sctx->regions);
    ff_framesync_uninit(&sctx->fs);
}

static int stack_activate(AVFilterContext *avctx)
{
    StackBaseContext *sctx = avctx->priv;
    return ff_framesync_activate(&sctx->fs);
}

static const AVFilterPad stack_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

#define STACK_COMMON_OPTS                                               \
    { "inputs", "Set number of inputs", OFFSET(base.nb_inputs), AV_OPT_TYPE_INT, { .i64 = 2 }, 2, UINT16_MAX, .flags = FLAGS }, \
    { "shortest", "Force termination when the shortest input terminates", OFFSET(base.shortest), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },

#define DEFINE_HSTACK_OPTIONS(api)                                      \
    static const AVOption hstack_##api##_options[] = {                  \
        STACK_COMMON_OPTS                                               \
        { "height", "Set output height (0 to use the height of input 0)", OFFSET(base.tile_height), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, UINT16_MAX, FLAGS }, \
        { NULL }                                                        \
    }

#define DEFINE_VSTACK_OPTIONS(api)                                      \
    static const AVOption vstack_##api##_options[] = {                  \
        STACK_COMMON_OPTS                                               \
        { "width",   "Set output width (0 to use the width of input 0)", OFFSET(base.tile_width), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, UINT16_MAX, FLAGS }, \
        { NULL }                                                        \
    }

#define DEFINE_XSTACK_OPTIONS(api)                                      \
    static const AVOption xstack_##api##_options[] = {                  \
        STACK_COMMON_OPTS                                               \
        { "layout", "Set custom layout", OFFSET(base.layout), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, .flags = FLAGS }, \
        { "grid",   "set fixed size grid layout", OFFSET(base.nb_grid_columns), AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, .flags = FLAGS }, \
        { "grid_tile_size",   "set tile size in grid layout", OFFSET(base.tile_width), AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, .flags = FLAGS }, \
        { "fill",   "Set the color for unused pixels", OFFSET(base.fillcolor_str), AV_OPT_TYPE_STRING, {.str = "none"}, .flags = FLAGS }, \
        { NULL }                                                        \
    }

#define DEFINE_STACK_FILTER(category, api, capi, filter_flags)          \
    static const AVClass category##_##api##_class = {                   \
        .class_name = #category "_" #api,                               \
        .item_name  = av_default_item_name,                             \
        .option     = category##_##api##_options,                       \
        .version    = LIBAVUTIL_VERSION_INT,                            \
    };                                                                  \
    const FFFilter ff_vf_##category##_##api = {                         \
        .p.name         = #category "_" #api,                           \
        .p.description  = NULL_IF_CONFIG_SMALL(#capi " " #category),    \
        .p.flags        = AVFILTER_FLAG_DYNAMIC_INPUTS | filter_flags,  \
        .p.priv_class   = &category##_##api##_class,                    \
        .priv_size      = sizeof(StackHWContext),                       \
        .init           = api##_stack_init,                             \
        .uninit         = api##_stack_uninit,                           \
        .activate       = stack_activate,                               \
        FILTER_PIXFMTS_ARRAY(api ## _stack_pix_fmts),                   \
        FILTER_OUTPUTS(stack_outputs),                                  \
        .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,                 \
    }
