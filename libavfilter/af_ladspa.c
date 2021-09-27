/*
 * Copyright (c) 2013 Paul B Mahol
 * Copyright (c) 2011 Mina Nagy Zaki
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
 * LADSPA wrapper
 */

#include <dlfcn.h>
#include <ladspa.h>
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "internal.h"

typedef struct LADSPAContext {
    const AVClass *class;
    char *dl_name;
    char *plugin;
    char *options;
    void *dl_handle;

    unsigned long nb_inputs;
    unsigned long *ipmap;      /* map input number to port number */

    unsigned long nb_inputcontrols;
    unsigned long *icmap;      /* map input control number to port number */
    LADSPA_Data *ictlv;        /* input controls values */

    unsigned long nb_outputs;
    unsigned long *opmap;      /* map output number to port number */

    unsigned long nb_outputcontrols;
    unsigned long *ocmap;      /* map output control number to port number */
    LADSPA_Data *octlv;        /* output controls values */

    const LADSPA_Descriptor *desc;
    int *ctl_needs_value;
    int nb_handles;
    LADSPA_Handle *handles;

    int sample_rate;
    int nb_samples;
    int64_t pts;
    int64_t duration;
    int in_trim;
    int out_pad;
    int latency;
} LADSPAContext;

#define OFFSET(x) offsetof(LADSPAContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
static const AVOption ladspa_options[] = {
    { "file", "set library name or full path", OFFSET(dl_name), AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "f",    "set library name or full path", OFFSET(dl_name), AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "plugin", "set plugin name", OFFSET(plugin), AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "p",      "set plugin name", OFFSET(plugin), AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "controls", "set plugin options", OFFSET(options), AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "c",        "set plugin options", OFFSET(options), AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "sample_rate", "set sample rate", OFFSET(sample_rate), AV_OPT_TYPE_INT, {.i64=44100}, 1, INT32_MAX, FLAGS },
    { "s",           "set sample rate", OFFSET(sample_rate), AV_OPT_TYPE_INT, {.i64=44100}, 1, INT32_MAX, FLAGS },
    { "nb_samples", "set the number of samples per requested frame", OFFSET(nb_samples), AV_OPT_TYPE_INT, {.i64=1024}, 1, INT_MAX, FLAGS },
    { "n",          "set the number of samples per requested frame", OFFSET(nb_samples), AV_OPT_TYPE_INT, {.i64=1024}, 1, INT_MAX, FLAGS },
    { "duration", "set audio duration", OFFSET(duration), AV_OPT_TYPE_DURATION, {.i64=-1}, -1, INT64_MAX, FLAGS },
    { "d",        "set audio duration", OFFSET(duration), AV_OPT_TYPE_DURATION, {.i64=-1}, -1, INT64_MAX, FLAGS },
    { "latency", "enable latency compensation", OFFSET(latency), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "l",       "enable latency compensation", OFFSET(latency), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(ladspa);

static int find_latency(AVFilterContext *ctx, LADSPAContext *s)
{
    int latency = 0;

    for (int ctl = 0; ctl < s->nb_outputcontrols; ctl++) {
        if (av_strcasecmp("latency", s->desc->PortNames[s->ocmap[ctl]]))
            continue;

        latency = lrintf(s->octlv[ctl]);
        break;
    }

    return latency;
}

static void print_ctl_info(AVFilterContext *ctx, int level,
                           LADSPAContext *s, int ctl, unsigned long *map,
                           LADSPA_Data *values, int print)
{
    const LADSPA_PortRangeHint *h = s->desc->PortRangeHints + map[ctl];

    av_log(ctx, level, "c%i: %s [", ctl, s->desc->PortNames[map[ctl]]);

    if (LADSPA_IS_HINT_TOGGLED(h->HintDescriptor)) {
        av_log(ctx, level, "toggled (1 or 0)");

        if (LADSPA_IS_HINT_HAS_DEFAULT(h->HintDescriptor))
            av_log(ctx, level, " (default %i)", (int)values[ctl]);
    } else {
        if (LADSPA_IS_HINT_INTEGER(h->HintDescriptor)) {
            av_log(ctx, level, "<int>");

            if (LADSPA_IS_HINT_BOUNDED_BELOW(h->HintDescriptor))
                av_log(ctx, level, ", min: %i", (int)h->LowerBound);

            if (LADSPA_IS_HINT_BOUNDED_ABOVE(h->HintDescriptor))
                av_log(ctx, level, ", max: %i", (int)h->UpperBound);

            if (print)
                av_log(ctx, level, " (value %d)", (int)values[ctl]);
            else if (LADSPA_IS_HINT_HAS_DEFAULT(h->HintDescriptor))
                av_log(ctx, level, " (default %d)", (int)values[ctl]);
        } else {
            av_log(ctx, level, "<float>");

            if (LADSPA_IS_HINT_BOUNDED_BELOW(h->HintDescriptor))
                av_log(ctx, level, ", min: %f", h->LowerBound);

            if (LADSPA_IS_HINT_BOUNDED_ABOVE(h->HintDescriptor))
                av_log(ctx, level, ", max: %f", h->UpperBound);

            if (print)
                av_log(ctx, level, " (value %f)", values[ctl]);
            else if (LADSPA_IS_HINT_HAS_DEFAULT(h->HintDescriptor))
                av_log(ctx, level, " (default %f)", values[ctl]);
        }

        if (LADSPA_IS_HINT_SAMPLE_RATE(h->HintDescriptor))
            av_log(ctx, level, ", multiple of sample rate");

        if (LADSPA_IS_HINT_LOGARITHMIC(h->HintDescriptor))
            av_log(ctx, level, ", logarithmic scale");
    }

    av_log(ctx, level, "]\n");
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    LADSPAContext *s = ctx->priv;
    AVFrame *out;
    int i, h, p, new_out_samples;

    av_assert0(in->channels == (s->nb_inputs * s->nb_handles));

    if (!s->nb_outputs ||
        (av_frame_is_writable(in) && s->nb_inputs == s->nb_outputs &&
         s->in_trim == 0 && s->out_pad == 0 &&
        !(s->desc->Properties & LADSPA_PROPERTY_INPLACE_BROKEN))) {
        out = in;
    } else {
        out = ff_get_audio_buffer(ctx->outputs[0], in->nb_samples);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    av_assert0(!s->nb_outputs || out->channels == (s->nb_outputs * s->nb_handles));

    for (h = 0; h < s->nb_handles; h++) {
        for (i = 0; i < s->nb_inputs; i++) {
            p = s->nb_handles > 1 ? h : i;
            s->desc->connect_port(s->handles[h], s->ipmap[i],
                                  (LADSPA_Data*)in->extended_data[p]);
        }

        for (i = 0; i < s->nb_outputs; i++) {
            p = s->nb_handles > 1 ? h : i;
            s->desc->connect_port(s->handles[h], s->opmap[i],
                                  (LADSPA_Data*)out->extended_data[p]);
        }

        s->desc->run(s->handles[h], in->nb_samples);
        if (s->latency)
            s->in_trim = s->out_pad = find_latency(ctx, s);
        s->latency = 0;
    }

    for (i = 0; i < s->nb_outputcontrols; i++)
        print_ctl_info(ctx, AV_LOG_VERBOSE, s, i, s->ocmap, s->octlv, 1);

    if (out != in)
        av_frame_free(&in);

    new_out_samples = out->nb_samples;
    if (s->in_trim > 0) {
        int trim = FFMIN(new_out_samples, s->in_trim);

        new_out_samples -= trim;
        s->in_trim -= trim;
    }

    if (new_out_samples <= 0) {
        av_frame_free(&out);
        return 0;
    } else if (new_out_samples < out->nb_samples) {
        int offset = out->nb_samples - new_out_samples;
        for (int ch = 0; ch < out->channels; ch++)
            memmove(out->extended_data[ch], out->extended_data[ch] + sizeof(float) * offset,
                    sizeof(float) * new_out_samples);
        out->nb_samples = new_out_samples;
    }

    return ff_filter_frame(ctx->outputs[0], out);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    LADSPAContext *s = ctx->priv;
    AVFrame *out;
    int64_t t;
    int i;

    if (ctx->nb_inputs) {
        int ret = ff_request_frame(ctx->inputs[0]);

        if (ret == AVERROR_EOF && s->out_pad > 0) {
            AVFrame *frame = ff_get_audio_buffer(outlink, FFMIN(2048, s->out_pad));
            if (!frame)
                return AVERROR(ENOMEM);

            s->out_pad -= frame->nb_samples;
            return filter_frame(ctx->inputs[0], frame);
        }
        return ret;
    }

    t = av_rescale(s->pts, AV_TIME_BASE, s->sample_rate);
    if (s->duration >= 0 && t >= s->duration)
        return AVERROR_EOF;

    out = ff_get_audio_buffer(outlink, s->nb_samples);
    if (!out)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->nb_outputs; i++)
        s->desc->connect_port(s->handles[0], s->opmap[i],
                (LADSPA_Data*)out->extended_data[i]);

    s->desc->run(s->handles[0], s->nb_samples);

    for (i = 0; i < s->nb_outputcontrols; i++)
        print_ctl_info(ctx, AV_LOG_INFO, s, i, s->ocmap, s->octlv, 1);

    out->sample_rate = s->sample_rate;
    out->pts         = s->pts;
    s->pts          += s->nb_samples;

    return ff_filter_frame(outlink, out);
}

static void set_default_ctl_value(LADSPAContext *s, int ctl,
                                  unsigned long *map, LADSPA_Data *values)
{
    const LADSPA_PortRangeHint *h = s->desc->PortRangeHints + map[ctl];
    const LADSPA_Data lower = h->LowerBound;
    const LADSPA_Data upper = h->UpperBound;

    if (LADSPA_IS_HINT_DEFAULT_MINIMUM(h->HintDescriptor)) {
        values[ctl] = lower;
    } else if (LADSPA_IS_HINT_DEFAULT_MAXIMUM(h->HintDescriptor)) {
        values[ctl] = upper;
    } else if (LADSPA_IS_HINT_DEFAULT_0(h->HintDescriptor)) {
        values[ctl] = 0.0;
    } else if (LADSPA_IS_HINT_DEFAULT_1(h->HintDescriptor)) {
        values[ctl] = 1.0;
    } else if (LADSPA_IS_HINT_DEFAULT_100(h->HintDescriptor)) {
        values[ctl] = 100.0;
    } else if (LADSPA_IS_HINT_DEFAULT_440(h->HintDescriptor)) {
        values[ctl] = 440.0;
    } else if (LADSPA_IS_HINT_DEFAULT_LOW(h->HintDescriptor)) {
        if (LADSPA_IS_HINT_LOGARITHMIC(h->HintDescriptor))
            values[ctl] = exp(log(lower) * 0.75 + log(upper) * 0.25);
        else
            values[ctl] = lower * 0.75 + upper * 0.25;
    } else if (LADSPA_IS_HINT_DEFAULT_MIDDLE(h->HintDescriptor)) {
        if (LADSPA_IS_HINT_LOGARITHMIC(h->HintDescriptor))
            values[ctl] = exp(log(lower) * 0.5 + log(upper) * 0.5);
        else
            values[ctl] = lower * 0.5 + upper * 0.5;
    } else if (LADSPA_IS_HINT_DEFAULT_HIGH(h->HintDescriptor)) {
        if (LADSPA_IS_HINT_LOGARITHMIC(h->HintDescriptor))
            values[ctl] = exp(log(lower) * 0.25 + log(upper) * 0.75);
        else
            values[ctl] = lower * 0.25 + upper * 0.75;
    }
}

static int connect_ports(AVFilterContext *ctx, AVFilterLink *link)
{
    LADSPAContext *s = ctx->priv;
    int i, j;

    s->nb_handles = s->nb_inputs == 1 && s->nb_outputs == 1 ? link->channels : 1;
    s->handles    = av_calloc(s->nb_handles, sizeof(*s->handles));
    if (!s->handles)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->nb_handles; i++) {
        s->handles[i] = s->desc->instantiate(s->desc, link->sample_rate);
        if (!s->handles[i]) {
            av_log(ctx, AV_LOG_ERROR, "Could not instantiate plugin.\n");
            return AVERROR_EXTERNAL;
        }

        // Connect the input control ports
        for (j = 0; j < s->nb_inputcontrols; j++)
            s->desc->connect_port(s->handles[i], s->icmap[j], s->ictlv + j);

        // Connect the output control ports
        for (j = 0; j < s->nb_outputcontrols; j++)
            s->desc->connect_port(s->handles[i], s->ocmap[j], &s->octlv[j]);

        if (s->desc->activate)
            s->desc->activate(s->handles[i]);
    }

    av_log(ctx, AV_LOG_DEBUG, "handles: %d\n", s->nb_handles);

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;

    return connect_ports(ctx, inlink);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    LADSPAContext *s = ctx->priv;
    int ret;

    if (ctx->nb_inputs) {
        AVFilterLink *inlink = ctx->inputs[0];

        outlink->format      = inlink->format;
        outlink->sample_rate = inlink->sample_rate;
        if (s->nb_inputs == s->nb_outputs) {
            outlink->channel_layout = inlink->channel_layout;
            outlink->channels = inlink->channels;
        }

        ret = 0;
    } else {
        outlink->sample_rate = s->sample_rate;
        outlink->time_base   = (AVRational){1, s->sample_rate};

        ret = connect_ports(ctx, outlink);
    }

    return ret;
}

static void count_ports(const LADSPA_Descriptor *desc,
                        unsigned long *nb_inputs, unsigned long *nb_outputs)
{
    LADSPA_PortDescriptor pd;
    int i;

    for (i = 0; i < desc->PortCount; i++) {
        pd = desc->PortDescriptors[i];

        if (LADSPA_IS_PORT_AUDIO(pd)) {
            if (LADSPA_IS_PORT_INPUT(pd)) {
                (*nb_inputs)++;
            } else if (LADSPA_IS_PORT_OUTPUT(pd)) {
                (*nb_outputs)++;
            }
        }
    }
}

static void *try_load(const char *dir, const char *soname)
{
    char *path = av_asprintf("%s/%s.so", dir, soname);
    void *ret = NULL;

    if (path) {
        ret = dlopen(path, RTLD_LOCAL|RTLD_NOW);
        av_free(path);
    }

    return ret;
}

static int set_control(AVFilterContext *ctx, unsigned long port, LADSPA_Data value)
{
    LADSPAContext *s = ctx->priv;
    const char *label = s->desc->Label;
    LADSPA_PortRangeHint *h = (LADSPA_PortRangeHint *)s->desc->PortRangeHints +
                              s->icmap[port];

    if (port >= s->nb_inputcontrols) {
        av_log(ctx, AV_LOG_ERROR, "Control c%ld is out of range [0 - %lu].\n",
               port, s->nb_inputcontrols);
        return AVERROR(EINVAL);
    }

    if (LADSPA_IS_HINT_BOUNDED_BELOW(h->HintDescriptor) &&
            value < h->LowerBound) {
        av_log(ctx, AV_LOG_ERROR,
                "%s: input control c%ld is below lower boundary of %0.4f.\n",
                label, port, h->LowerBound);
        return AVERROR(EINVAL);
    }

    if (LADSPA_IS_HINT_BOUNDED_ABOVE(h->HintDescriptor) &&
            value > h->UpperBound) {
        av_log(ctx, AV_LOG_ERROR,
                "%s: input control c%ld is above upper boundary of %0.4f.\n",
                label, port, h->UpperBound);
        return AVERROR(EINVAL);
    }

    s->ictlv[port] = value;

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    LADSPAContext *s = ctx->priv;
    LADSPA_Descriptor_Function descriptor_fn;
    const LADSPA_Descriptor *desc;
    LADSPA_PortDescriptor pd;
    AVFilterPad pad = { NULL };
    char *p, *arg, *saveptr = NULL;
    unsigned long nb_ports;
    int i, j = 0, ret;

    if (!s->dl_name) {
        av_log(ctx, AV_LOG_ERROR, "No plugin name provided\n");
        return AVERROR(EINVAL);
    }

    if (s->dl_name[0] == '/' || s->dl_name[0] == '.') {
        // argument is a path
        s->dl_handle = dlopen(s->dl_name, RTLD_LOCAL|RTLD_NOW);
    } else {
        // argument is a shared object name
        char *paths = av_strdup(getenv("LADSPA_PATH"));
        const char *home_path = getenv("HOME");
        const char *separator = ":";

        if (paths) {
            p = paths;
            while ((arg = av_strtok(p, separator, &saveptr)) && !s->dl_handle) {
                s->dl_handle = try_load(arg, s->dl_name);
                p = NULL;
            }
        }

        av_free(paths);
        if (!s->dl_handle && home_path && (paths = av_asprintf("%s/.ladspa", home_path))) {
            s->dl_handle = try_load(paths, s->dl_name);
            av_free(paths);
        }

        if (!s->dl_handle && home_path && (paths = av_asprintf("%s/.ladspa/lib", home_path))) {
            s->dl_handle = try_load(paths, s->dl_name);
            av_free(paths);
        }

        if (!s->dl_handle)
            s->dl_handle = try_load("/usr/local/lib/ladspa", s->dl_name);

        if (!s->dl_handle)
            s->dl_handle = try_load("/usr/lib/ladspa", s->dl_name);
    }
    if (!s->dl_handle) {
        av_log(ctx, AV_LOG_ERROR, "Failed to load '%s'\n", s->dl_name);
        return AVERROR(EINVAL);
    }

    descriptor_fn = dlsym(s->dl_handle, "ladspa_descriptor");
    if (!descriptor_fn) {
        av_log(ctx, AV_LOG_ERROR, "Could not find ladspa_descriptor: %s\n", dlerror());
        return AVERROR(EINVAL);
    }

    // Find the requested plugin, or list plugins
    if (!s->plugin) {
        av_log(ctx, AV_LOG_INFO, "The '%s' library contains the following plugins:\n", s->dl_name);
        av_log(ctx, AV_LOG_INFO, "I = Input Channels\n");
        av_log(ctx, AV_LOG_INFO, "O = Output Channels\n");
        av_log(ctx, AV_LOG_INFO, "I:O %-25s %s\n", "Plugin", "Description");
        av_log(ctx, AV_LOG_INFO, "\n");
        for (i = 0; desc = descriptor_fn(i); i++) {
            unsigned long inputs = 0, outputs = 0;

            count_ports(desc, &inputs, &outputs);
            av_log(ctx, AV_LOG_INFO, "%lu:%lu %-25s %s\n", inputs, outputs, desc->Label,
                   (char *)av_x_if_null(desc->Name, "?"));
            av_log(ctx, AV_LOG_VERBOSE, "Maker: %s\n",
                   (char *)av_x_if_null(desc->Maker, "?"));
            av_log(ctx, AV_LOG_VERBOSE, "Copyright: %s\n",
                   (char *)av_x_if_null(desc->Copyright, "?"));
        }
        return AVERROR_EXIT;
    } else {
        for (i = 0;; i++) {
            desc = descriptor_fn(i);
            if (!desc) {
                av_log(ctx, AV_LOG_ERROR, "Could not find plugin: %s\n", s->plugin);
                return AVERROR(EINVAL);
            }

            if (desc->Label && !strcmp(desc->Label, s->plugin))
                break;
        }
    }

    s->desc  = desc;
    nb_ports = desc->PortCount;

    s->ipmap = av_calloc(nb_ports, sizeof(*s->ipmap));
    s->opmap = av_calloc(nb_ports, sizeof(*s->opmap));
    s->icmap = av_calloc(nb_ports, sizeof(*s->icmap));
    s->ocmap = av_calloc(nb_ports, sizeof(*s->ocmap));
    s->ictlv = av_calloc(nb_ports, sizeof(*s->ictlv));
    s->octlv = av_calloc(nb_ports, sizeof(*s->octlv));
    s->ctl_needs_value = av_calloc(nb_ports, sizeof(*s->ctl_needs_value));
    if (!s->ipmap || !s->opmap || !s->icmap ||
        !s->ocmap || !s->ictlv || !s->octlv || !s->ctl_needs_value)
        return AVERROR(ENOMEM);

    for (i = 0; i < nb_ports; i++) {
        pd = desc->PortDescriptors[i];

        if (LADSPA_IS_PORT_AUDIO(pd)) {
            if (LADSPA_IS_PORT_INPUT(pd)) {
                s->ipmap[s->nb_inputs] = i;
                s->nb_inputs++;
            } else if (LADSPA_IS_PORT_OUTPUT(pd)) {
                s->opmap[s->nb_outputs] = i;
                s->nb_outputs++;
            }
        } else if (LADSPA_IS_PORT_CONTROL(pd)) {
            if (LADSPA_IS_PORT_INPUT(pd)) {
                s->icmap[s->nb_inputcontrols] = i;

                if (LADSPA_IS_HINT_HAS_DEFAULT(desc->PortRangeHints[i].HintDescriptor))
                    set_default_ctl_value(s, s->nb_inputcontrols, s->icmap, s->ictlv);
                else
                    s->ctl_needs_value[s->nb_inputcontrols] = 1;

                s->nb_inputcontrols++;
            } else if (LADSPA_IS_PORT_OUTPUT(pd)) {
                s->ocmap[s->nb_outputcontrols] = i;
                s->nb_outputcontrols++;
            }
        }
    }

    // List Control Ports if "help" is specified
    if (s->options && !strcmp(s->options, "help")) {
        if (!s->nb_inputcontrols) {
            av_log(ctx, AV_LOG_INFO,
                   "The '%s' plugin does not have any input controls.\n",
                   desc->Label);
        } else {
            av_log(ctx, AV_LOG_INFO,
                   "The '%s' plugin has the following input controls:\n",
                   desc->Label);
            for (i = 0; i < s->nb_inputcontrols; i++)
                print_ctl_info(ctx, AV_LOG_INFO, s, i, s->icmap, s->ictlv, 0);
        }
        return AVERROR_EXIT;
    }

    // Parse control parameters
    p = s->options;
    while (s->options) {
        LADSPA_Data val;
        int ret;

        if (!(arg = av_strtok(p, " |", &saveptr)))
            break;
        p = NULL;

        if (av_sscanf(arg, "c%d=%f", &i, &val) != 2) {
            if (av_sscanf(arg, "%f", &val) != 1) {
                av_log(ctx, AV_LOG_ERROR, "Invalid syntax.\n");
                return AVERROR(EINVAL);
            }
            i = j++;
        }

        if ((ret = set_control(ctx, i, val)) < 0)
            return ret;
        s->ctl_needs_value[i] = 0;
    }

    // Check if any controls are not set
    for (i = 0; i < s->nb_inputcontrols; i++) {
        if (s->ctl_needs_value[i]) {
            av_log(ctx, AV_LOG_ERROR, "Control c%d must be set.\n", i);
            print_ctl_info(ctx, AV_LOG_ERROR, s, i, s->icmap, s->ictlv, 0);
            return AVERROR(EINVAL);
        }
    }

    pad.type = AVMEDIA_TYPE_AUDIO;

    if (s->nb_inputs) {
        pad.name = av_asprintf("in0:%s%lu", desc->Label, s->nb_inputs);
        if (!pad.name)
            return AVERROR(ENOMEM);

        pad.filter_frame = filter_frame;
        pad.config_props = config_input;
        if ((ret = ff_append_inpad_free_name(ctx, &pad)) < 0)
            return ret;
    }

    av_log(ctx, AV_LOG_DEBUG, "ports: %lu\n", nb_ports);
    av_log(ctx, AV_LOG_DEBUG, "inputs: %lu outputs: %lu\n",
                              s->nb_inputs, s->nb_outputs);
    av_log(ctx, AV_LOG_DEBUG, "input controls: %lu output controls: %lu\n",
                              s->nb_inputcontrols, s->nb_outputcontrols);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    LADSPAContext *s = ctx->priv;
    AVFilterChannelLayouts *layouts;
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

    if (s->nb_inputs == 1 && s->nb_outputs == 1) {
        // We will instantiate multiple LADSPA_Handle, one over each channel
        ret = ff_set_common_all_channel_counts(ctx);
        if (ret < 0)
            return ret;
    } else if (s->nb_inputs == 2 && s->nb_outputs == 2) {
        layouts = NULL;
        ret = ff_add_channel_layout(&layouts, AV_CH_LAYOUT_STEREO);
        if (ret < 0)
            return ret;
        ret = ff_set_common_channel_layouts(ctx, layouts);
        if (ret < 0)
            return ret;
    } else {
        AVFilterLink *outlink = ctx->outputs[0];

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
    LADSPAContext *s = ctx->priv;
    int i;

    for (i = 0; i < s->nb_handles; i++) {
        if (s->desc->deactivate)
            s->desc->deactivate(s->handles[i]);
        if (s->desc->cleanup)
            s->desc->cleanup(s->handles[i]);
    }

    if (s->dl_handle)
        dlclose(s->dl_handle);

    av_freep(&s->ipmap);
    av_freep(&s->opmap);
    av_freep(&s->icmap);
    av_freep(&s->ocmap);
    av_freep(&s->ictlv);
    av_freep(&s->octlv);
    av_freep(&s->handles);
    av_freep(&s->ctl_needs_value);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    LADSPA_Data value;
    unsigned long port;

    if (av_sscanf(cmd, "c%ld", &port) + av_sscanf(args, "%f", &value) != 2)
        return AVERROR(EINVAL);

    return set_control(ctx, port, value);
}

static const AVFilterPad ladspa_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
};

const AVFilter ff_af_ladspa = {
    .name          = "ladspa",
    .description   = NULL_IF_CONFIG_SMALL("Apply LADSPA effect."),
    .priv_size     = sizeof(LADSPAContext),
    .priv_class    = &ladspa_class,
    .init          = init,
    .uninit        = uninit,
    .process_command = process_command,
    .inputs        = 0,
    FILTER_OUTPUTS(ladspa_outputs),
    FILTER_QUERY_FUNC(query_formats),
    .flags         = AVFILTER_FLAG_DYNAMIC_INPUTS,
};
