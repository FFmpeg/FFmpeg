/*
 * Copyright (c) 2017 Paul B Mahol
 * Copyright (c) 2007-2016 David Robillard <http://drobilla.net>
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

/**
 * @file
 * LV2 wrapper
 */

#include <lilv/lilv.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/buf-size/buf-size.h>

#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "internal.h"

typedef struct URITable {
    char    **uris;
    size_t    n_uris;
} URITable;

typedef struct LV2Context {
    const AVClass *class;
    char *plugin_uri;
    char *options;

    unsigned nb_inputs;
    unsigned nb_inputcontrols;
    unsigned nb_outputs;

    int sample_rate;
    int nb_samples;
    int64_t pts;
    int64_t duration;

    LilvWorld         *world;
    const LilvPlugin  *plugin;
    uint32_t           nb_ports;
    float             *values;
    URITable           uri_table;
    LV2_URID_Map       map;
    LV2_Feature        map_feature;
    LV2_URID_Unmap     unmap;
    LV2_Feature        unmap_feature;
    LV2_Atom_Sequence  seq_in[2];
    LV2_Atom_Sequence *seq_out;
    const LV2_Feature *features[5];

    float *mins;
    float *maxes;
    float *controls;

    LilvInstance *instance;

    LilvNode  *atom_AtomPort;
    LilvNode  *atom_Sequence;
    LilvNode  *lv2_AudioPort;
    LilvNode  *lv2_CVPort;
    LilvNode  *lv2_ControlPort;
    LilvNode  *lv2_Optional;
    LilvNode  *lv2_InputPort;
    LilvNode  *lv2_OutputPort;
    LilvNode  *urid_map;
    LilvNode  *powerOf2BlockLength;
    LilvNode  *fixedBlockLength;
    LilvNode  *boundedBlockLength;
} LV2Context;

#define OFFSET(x) offsetof(LV2Context, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM

static const AVOption lv2_options[] = {
    { "plugin", "set plugin uri", OFFSET(plugin_uri), AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "p",      "set plugin uri", OFFSET(plugin_uri), AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "controls", "set plugin options", OFFSET(options), AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "c",        "set plugin options", OFFSET(options), AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "sample_rate", "set sample rate", OFFSET(sample_rate), AV_OPT_TYPE_INT, {.i64=44100}, 1, INT32_MAX, FLAGS },
    { "s",           "set sample rate", OFFSET(sample_rate), AV_OPT_TYPE_INT, {.i64=44100}, 1, INT32_MAX, FLAGS },
    { "nb_samples", "set the number of samples per requested frame", OFFSET(nb_samples), AV_OPT_TYPE_INT, {.i64=1024}, 1, INT_MAX, FLAGS },
    { "n",          "set the number of samples per requested frame", OFFSET(nb_samples), AV_OPT_TYPE_INT, {.i64=1024}, 1, INT_MAX, FLAGS },
    { "duration", "set audio duration", OFFSET(duration), AV_OPT_TYPE_DURATION, {.i64=-1}, -1, INT64_MAX, FLAGS },
    { "d",        "set audio duration", OFFSET(duration), AV_OPT_TYPE_DURATION, {.i64=-1}, -1, INT64_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(lv2);

static void uri_table_init(URITable *table)
{
    table->uris   = NULL;
    table->n_uris = 0;
}

static void uri_table_destroy(URITable *table)
{
    int i;

    for (i = 0; i < table->n_uris; i++) {
        av_freep(&table->uris[i]);
    }

    av_freep(&table->uris);
}

static LV2_URID uri_table_map(LV2_URID_Map_Handle handle, const char *uri)
{
    URITable *table = (URITable*)handle;
    const size_t len = strlen(uri);
    size_t i;
    char **tmp;

    for (i = 0; i < table->n_uris; i++) {
        if (!strcmp(table->uris[i], uri)) {
            return i + 1;
        }
    }

    tmp = av_calloc(table->n_uris + 1, sizeof(char*));
    if (!tmp)
        return table->n_uris;
    memcpy(tmp, table->uris, table->n_uris * sizeof(char**));

    av_free(table->uris);
    table->uris = tmp;
    table->uris[table->n_uris] = av_malloc(len + 1);
    if (!table->uris[table->n_uris])
        return table->n_uris;

    memcpy(table->uris[table->n_uris], uri, len + 1);
    table->n_uris++;

    return table->n_uris;
}

static const char *uri_table_unmap(LV2_URID_Map_Handle handle, LV2_URID urid)
{
    URITable *table = (URITable*)handle;

    if (urid > 0 && urid <= table->n_uris) {
        return table->uris[urid - 1];
    }

    return NULL;
}

static void connect_ports(LV2Context *s, AVFrame *in, AVFrame *out)
{
    int ich = 0, och = 0, i;

    for (i = 0; i < s->nb_ports; i++) {
        const LilvPort *port = lilv_plugin_get_port_by_index(s->plugin, i);

        if (lilv_port_is_a(s->plugin, port, s->lv2_AudioPort) ||
            lilv_port_is_a(s->plugin, port, s->lv2_CVPort)) {
            if (lilv_port_is_a(s->plugin, port, s->lv2_InputPort)) {
                lilv_instance_connect_port(s->instance, i, in->extended_data[ich++]);
            } else if (lilv_port_is_a(s->plugin, port, s->lv2_OutputPort)) {
                lilv_instance_connect_port(s->instance, i, out->extended_data[och++]);
            } else {
                av_log(s, AV_LOG_WARNING, "port %d neither input nor output, skipping\n", i);
            }
        } else if (lilv_port_is_a(s->plugin, port, s->atom_AtomPort)) {
            if (lilv_port_is_a(s->plugin, port, s->lv2_InputPort)) {
                lilv_instance_connect_port(s->instance, i, &s->seq_in);
            } else {
                lilv_instance_connect_port(s->instance, i, s->seq_out);
            }
        } else if (lilv_port_is_a(s->plugin, port, s->lv2_ControlPort)) {
            lilv_instance_connect_port(s->instance, i, &s->controls[i]);
        }
    }

    s->seq_in[0].atom.size = sizeof(LV2_Atom_Sequence_Body);
    s->seq_in[0].atom.type = uri_table_map(&s->uri_table, LV2_ATOM__Sequence);
    s->seq_out->atom.size  = 9624;
    s->seq_out->atom.type  = uri_table_map(&s->uri_table, LV2_ATOM__Chunk);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    LV2Context *s = ctx->priv;
    AVFrame *out;

    if (!s->nb_outputs ||
        (av_frame_is_writable(in) && s->nb_inputs == s->nb_outputs)) {
        out = in;
    } else {
        out = ff_get_audio_buffer(ctx->outputs[0], in->nb_samples);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    connect_ports(s, in, out);

    lilv_instance_run(s->instance, in->nb_samples);

    if (out != in)
        av_frame_free(&in);

    return ff_filter_frame(ctx->outputs[0], out);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    LV2Context *s = ctx->priv;
    AVFrame *out;
    int64_t t;

    if (ctx->nb_inputs)
        return ff_request_frame(ctx->inputs[0]);

    t = av_rescale(s->pts, AV_TIME_BASE, s->sample_rate);
    if (s->duration >= 0 && t >= s->duration)
        return AVERROR_EOF;

    out = ff_get_audio_buffer(outlink, s->nb_samples);
    if (!out)
        return AVERROR(ENOMEM);

    connect_ports(s, out, out);

    lilv_instance_run(s->instance, out->nb_samples);

    out->sample_rate = s->sample_rate;
    out->pts         = s->pts;
    s->pts          += s->nb_samples;

    return ff_filter_frame(outlink, out);
}

static const LV2_Feature buf_size_features[3] = {
    { LV2_BUF_SIZE__powerOf2BlockLength, NULL },
    { LV2_BUF_SIZE__fixedBlockLength,    NULL },
    { LV2_BUF_SIZE__boundedBlockLength,  NULL },
};

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    LV2Context *s = ctx->priv;
    char *p, *arg, *saveptr = NULL;
    int i, sample_rate;

    uri_table_init(&s->uri_table);
    s->map.handle = &s->uri_table;
    s->map.map = uri_table_map;
    s->map_feature.URI = LV2_URID_MAP_URI;
    s->map_feature.data = &s->map;
    s->unmap.handle = &s->uri_table;
    s->unmap.unmap  = uri_table_unmap;
    s->unmap_feature.URI = LV2_URID_UNMAP_URI;
    s->unmap_feature.data = &s->unmap;
    s->features[0] = &s->map_feature;
    s->features[1] = &s->unmap_feature;
    s->features[2] = &buf_size_features[0];
    s->features[3] = &buf_size_features[1];
    s->features[4] = &buf_size_features[2];

    if (ctx->nb_inputs) {
        AVFilterLink *inlink = ctx->inputs[0];

        outlink->format      = inlink->format;
        outlink->sample_rate = sample_rate = inlink->sample_rate;
        if (s->nb_inputs == s->nb_outputs) {
            outlink->channel_layout = inlink->channel_layout;
            outlink->channels = inlink->channels;
        }

    } else {
        outlink->sample_rate = sample_rate = s->sample_rate;
        outlink->time_base   = (AVRational){1, s->sample_rate};
    }

    s->instance = lilv_plugin_instantiate(s->plugin, sample_rate, s->features);
    if (!s->instance) {
        av_log(s, AV_LOG_ERROR, "Failed to instantiate <%s>\n", lilv_node_as_uri(lilv_plugin_get_uri(s->plugin)));
        return AVERROR(EINVAL);
    }

    s->mins     = av_calloc(s->nb_ports, sizeof(float));
    s->maxes    = av_calloc(s->nb_ports, sizeof(float));
    s->controls = av_calloc(s->nb_ports, sizeof(float));

    if (!s->mins || !s->maxes || !s->controls)
        return AVERROR(ENOMEM);

    lilv_plugin_get_port_ranges_float(s->plugin, s->mins, s->maxes, s->controls);
    s->seq_out = av_malloc(sizeof(LV2_Atom_Sequence) + 9624);
    if (!s->seq_out)
        return AVERROR(ENOMEM);

    if (s->options && !strcmp(s->options, "help")) {
        if (!s->nb_inputcontrols) {
            av_log(ctx, AV_LOG_INFO,
                   "The '%s' plugin does not have any input controls.\n",
                   s->plugin_uri);
        } else {
            av_log(ctx, AV_LOG_INFO,
                   "The '%s' plugin has the following input controls:\n",
                   s->plugin_uri);
            for (i = 0; i < s->nb_ports; i++) {
                const LilvPort *port = lilv_plugin_get_port_by_index(s->plugin, i);
                const LilvNode *symbol = lilv_port_get_symbol(s->plugin, port);
                LilvNode *name = lilv_port_get_name(s->plugin, port);

                if (lilv_port_is_a(s->plugin, port, s->lv2_InputPort) &&
                    lilv_port_is_a(s->plugin, port, s->lv2_ControlPort)) {
                    av_log(ctx, AV_LOG_INFO, "%s\t\t<float> (from %f to %f) (default %f)\t\t%s\n",
                           lilv_node_as_string(symbol), s->mins[i], s->maxes[i], s->controls[i],
                           lilv_node_as_string(name));
                }

                lilv_node_free(name);
            }
        }
        return AVERROR_EXIT;
    }

    p = s->options;
    while (s->options) {
        const LilvPort *port;
        LilvNode *sym;
        float val;
        char *str, *vstr;
        int index;

        if (!(arg = av_strtok(p, " |", &saveptr)))
            break;
        p = NULL;

        vstr = strstr(arg, "=");
        if (vstr == NULL) {
            av_log(ctx, AV_LOG_ERROR, "Invalid syntax.\n");
            return AVERROR(EINVAL);
        }

        vstr[0] = 0;
        str  = arg;
        val  = atof(vstr+1);
        sym  = lilv_new_string(s->world, str);
        port = lilv_plugin_get_port_by_symbol(s->plugin, sym);
        lilv_node_free(sym);
        if (!port) {
            av_log(s, AV_LOG_WARNING, "Unknown option: <%s>\n", str);
        } else {
            index = lilv_port_get_index(s->plugin, port);
            s->controls[index] = val;
        }
    }

    if (s->nb_inputs &&
        (lilv_plugin_has_feature(s->plugin, s->powerOf2BlockLength) ||
         lilv_plugin_has_feature(s->plugin, s->fixedBlockLength) ||
         lilv_plugin_has_feature(s->plugin, s->boundedBlockLength))) {
        AVFilterLink *inlink = ctx->inputs[0];

        inlink->min_samples = inlink->max_samples = 4096;
    }

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    LV2Context *s = ctx->priv;
    const LilvPlugins *plugins;
    const LilvPlugin *plugin;
    AVFilterPad pad = { NULL };
    LilvNode *uri;
    int i, ret;

    s->world = lilv_world_new();
    if (!s->world)
        return AVERROR(ENOMEM);

    uri = lilv_new_uri(s->world, s->plugin_uri);
    if (!uri) {
        av_log(s, AV_LOG_ERROR, "Invalid plugin URI <%s>\n", s->plugin_uri);
        return AVERROR(EINVAL);
    }

    lilv_world_load_all(s->world);
    plugins = lilv_world_get_all_plugins(s->world);
    plugin  = lilv_plugins_get_by_uri(plugins, uri);
    lilv_node_free(uri);

    if (!plugin) {
        av_log(s, AV_LOG_ERROR, "Plugin <%s> not found\n", s->plugin_uri);
        return AVERROR(EINVAL);
    }

    s->plugin = plugin;
    s->nb_ports = lilv_plugin_get_num_ports(s->plugin);

    s->lv2_InputPort       = lilv_new_uri(s->world, LV2_CORE__InputPort);
    s->lv2_OutputPort      = lilv_new_uri(s->world, LV2_CORE__OutputPort);
    s->lv2_AudioPort       = lilv_new_uri(s->world, LV2_CORE__AudioPort);
    s->lv2_ControlPort     = lilv_new_uri(s->world, LV2_CORE__ControlPort);
    s->lv2_Optional        = lilv_new_uri(s->world, LV2_CORE__connectionOptional);
    s->atom_AtomPort       = lilv_new_uri(s->world, LV2_ATOM__AtomPort);
    s->atom_Sequence       = lilv_new_uri(s->world, LV2_ATOM__Sequence);
    s->urid_map            = lilv_new_uri(s->world, LV2_URID__map);
    s->powerOf2BlockLength = lilv_new_uri(s->world, LV2_BUF_SIZE__powerOf2BlockLength);
    s->fixedBlockLength    = lilv_new_uri(s->world, LV2_BUF_SIZE__fixedBlockLength);
    s->boundedBlockLength  = lilv_new_uri(s->world, LV2_BUF_SIZE__boundedBlockLength);

    for (i = 0; i < s->nb_ports; i++) {
        const LilvPort *lport = lilv_plugin_get_port_by_index(s->plugin, i);
        int is_input = 0;
        int is_optional = 0;

        is_optional = lilv_port_has_property(s->plugin, lport, s->lv2_Optional);

        if (lilv_port_is_a(s->plugin, lport, s->lv2_InputPort)) {
            is_input = 1;
        } else if (!lilv_port_is_a(s->plugin, lport, s->lv2_OutputPort) && !is_optional) {
            return AVERROR(EINVAL);
        }

        if (lilv_port_is_a(s->plugin, lport, s->lv2_ControlPort)) {
            if (is_input) {
                s->nb_inputcontrols++;
            }
        } else if (lilv_port_is_a(s->plugin, lport, s->lv2_AudioPort)) {
            if (is_input) {
                s->nb_inputs++;
            } else {
                s->nb_outputs++;
            }
        }
    }

    pad.type = AVMEDIA_TYPE_AUDIO;

    if (s->nb_inputs) {
        pad.name = av_asprintf("in0:%s:%u", s->plugin_uri, s->nb_inputs);
        if (!pad.name)
            return AVERROR(ENOMEM);

        pad.filter_frame = filter_frame;
        if ((ret = ff_append_inpad_free_name(ctx, &pad)) < 0)
            return ret;
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    LV2Context *s = ctx->priv;
    AVFilterChannelLayouts *layouts;
    AVFilterLink *outlink = ctx->outputs[0];
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE };
    int ret = ff_set_common_formats_from_list(ctx, sample_fmts);
    if (ret < 0)
        return ret;

    if (s->nb_inputs) {
        ret = ff_set_common_all_samplerates(ctx);
        if (ret < 0)
            return ret;
    } else {
        int sample_rates[] = { s->sample_rate, -1 };

        ret = ff_set_common_samplerates_from_list(ctx, sample_rates);
        if (ret < 0)
            return ret;
    }

    if (s->nb_inputs == 2 && s->nb_outputs == 2) {
        layouts = NULL;
        ret = ff_add_channel_layout(&layouts, AV_CH_LAYOUT_STEREO);
        if (ret < 0)
            return ret;
        ret = ff_set_common_channel_layouts(ctx, layouts);
        if (ret < 0)
            return ret;
    } else {
        if (s->nb_inputs >= 1) {
            AVFilterLink *inlink = ctx->inputs[0];
            uint64_t inlayout = FF_COUNT2LAYOUT(s->nb_inputs);

            layouts = NULL;
            ret = ff_add_channel_layout(&layouts, inlayout);
            if (ret < 0)
                return ret;
            ret = ff_channel_layouts_ref(layouts, &inlink->outcfg.channel_layouts);
            if (ret < 0)
                return ret;

            if (!s->nb_outputs) {
                ret = ff_channel_layouts_ref(layouts, &outlink->incfg.channel_layouts);
                if (ret < 0)
                    return ret;
            }
        }

        if (s->nb_outputs >= 1) {
            uint64_t outlayout = FF_COUNT2LAYOUT(s->nb_outputs);

            layouts = NULL;
            ret = ff_add_channel_layout(&layouts, outlayout);
            if (ret < 0)
                return ret;
            ret = ff_channel_layouts_ref(layouts, &outlink->incfg.channel_layouts);
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    LV2Context *s = ctx->priv;

    lilv_node_free(s->powerOf2BlockLength);
    lilv_node_free(s->fixedBlockLength);
    lilv_node_free(s->boundedBlockLength);
    lilv_node_free(s->urid_map);
    lilv_node_free(s->atom_Sequence);
    lilv_node_free(s->atom_AtomPort);
    lilv_node_free(s->lv2_Optional);
    lilv_node_free(s->lv2_ControlPort);
    lilv_node_free(s->lv2_AudioPort);
    lilv_node_free(s->lv2_OutputPort);
    lilv_node_free(s->lv2_InputPort);
    uri_table_destroy(&s->uri_table);
    lilv_instance_free(s->instance);
    lilv_world_free(s->world);
    av_freep(&s->mins);
    av_freep(&s->maxes);
    av_freep(&s->controls);
    av_freep(&s->seq_out);
}

static const AVFilterPad lv2_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
};

const AVFilter ff_af_lv2 = {
    .name          = "lv2",
    .description   = NULL_IF_CONFIG_SMALL("Apply LV2 effect."),
    .priv_size     = sizeof(LV2Context),
    .priv_class    = &lv2_class,
    .init          = init,
    .uninit        = uninit,
    .inputs        = 0,
    FILTER_OUTPUTS(lv2_outputs),
    FILTER_QUERY_FUNC(query_formats),
    .flags         = AVFILTER_FLAG_DYNAMIC_INPUTS,
};
