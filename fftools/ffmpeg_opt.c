/*
 * ffmpeg option parsing
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

#include "config.h"

#include <stdint.h>

#if HAVE_SYS_RESOURCE_H
#include <sys/time.h>
#include <sys/resource.h>
#endif

#include "ffmpeg.h"
#include "ffmpeg_sched.h"
#include "cmdutils.h"
#include "opt_common.h"

#include "libavformat/avformat.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/bsf.h"

#include "libavfilter/avfilter.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/avutil.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/stereo3d.h"
#include "graph/graphprint.h"

HWDevice *filter_hw_device;

char *vstats_filename;

float dts_delta_threshold   = 10;
float dts_error_threshold   = 3600*30;

#if FFMPEG_OPT_VSYNC
enum VideoSyncMethod video_sync_method = VSYNC_AUTO;
#endif
float frame_drop_threshold = 0;
int do_benchmark      = 0;
int do_benchmark_all  = 0;
int do_hex_dump       = 0;
int do_pkt_dump       = 0;
int copy_ts           = 0;
int start_at_zero     = 0;
int copy_tb           = -1;
int debug_ts          = 0;
int exit_on_error     = 0;
int abort_on_flags    = 0;
int print_stats       = -1;
int stdin_interaction = 1;
float max_error_rate  = 2.0/3;
char *filter_nbthreads;
int filter_complex_nbthreads = 0;
int vstats_version = 2;
int print_graphs = 0;
char *print_graphs_file = NULL;
char *print_graphs_format = NULL;
int auto_conversion_filters = 1;
int64_t stats_period = 500000;


static int file_overwrite     = 0;
static int no_file_overwrite  = 0;
int ignore_unknown_streams = 0;
int copy_unknown_streams = 0;
int recast_media = 0;

// this struct is passed as the optctx argument
// to func_arg() for global options
typedef struct GlobalOptionsContext {
    Scheduler      *sch;

    char          **filtergraphs;
    int          nb_filtergraphs;
} GlobalOptionsContext;

static void uninit_options(OptionsContext *o)
{
    /* all OPT_SPEC and OPT_TYPE_STRING can be freed in generic way */
    for (const OptionDef *po = options; po->name; po++) {
        void *dst;

        if (!(po->flags & OPT_FLAG_OFFSET))
            continue;

        dst = (uint8_t*)o + po->u.off;
        if (po->flags & OPT_FLAG_SPEC) {
            SpecifierOptList *so = dst;
            for (int i = 0; i < so->nb_opt; i++) {
                av_freep(&so->opt[i].specifier);
                if (po->flags & OPT_FLAG_PERSTREAM)
                    stream_specifier_uninit(&so->opt[i].stream_spec);
                if (po->type == OPT_TYPE_STRING)
                    av_freep(&so->opt[i].u.str);
            }
            av_freep(&so->opt);
            so->nb_opt = 0;
        } else if (po->type == OPT_TYPE_STRING)
            av_freep(dst);
    }

    for (int i = 0; i < o->nb_stream_maps; i++)
        av_freep(&o->stream_maps[i].linklabel);
    av_freep(&o->stream_maps);

    for (int i = 0; i < o->nb_attachments; i++)
        av_freep(&o->attachments[i]);
    av_freep(&o->attachments);

    av_dict_free(&o->streamid);
}

static void init_options(OptionsContext *o)
{
    memset(o, 0, sizeof(*o));

    o->stop_time = INT64_MAX;
    o->mux_max_delay  = 0.7;
    o->start_time     = AV_NOPTS_VALUE;
    o->start_time_eof = AV_NOPTS_VALUE;
    o->recording_time = INT64_MAX;
    o->limit_filesize = INT64_MAX;
    o->chapters_input_file = INT_MAX;
    o->accurate_seek  = 1;
    o->thread_queue_size = 0;
    o->input_sync_ref = -1;
    o->find_stream_info = 1;
    o->shortest_buf_duration = 10.f;
}

static int show_hwaccels(void *optctx, const char *opt, const char *arg)
{
    enum AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;

    printf("Hardware acceleration methods:\n");
    while ((type = av_hwdevice_iterate_types(type)) !=
           AV_HWDEVICE_TYPE_NONE)
        printf("%s\n", av_hwdevice_get_type_name(type));
    printf("\n");
    return 0;
}

const char *opt_match_per_type_str(const SpecifierOptList *sol,
                                   char mediatype)
{
    av_assert0(!sol->nb_opt || sol->type == OPT_TYPE_STRING);

    for (int i = 0; i < sol->nb_opt; i++) {
        const char *spec = sol->opt[i].specifier;
        if (spec[0] == mediatype && !spec[1])
            return sol->opt[i].u.str;
    }
    return NULL;
}

static unsigned opt_match_per_stream(void *logctx, enum OptionType type,
                                     const SpecifierOptList *sol,
                                     AVFormatContext *fc, AVStream *st)
{
    int matches = 0, match_idx = -1;

    av_assert0((type == sol->type) || !sol->nb_opt);

    for (int i = 0; i < sol->nb_opt; i++) {
        const StreamSpecifier *ss = &sol->opt[i].stream_spec;

        if (stream_specifier_match(ss, fc, st, logctx)) {
            match_idx = i;
            matches++;
        }
    }

    if (matches > 1 && sol->opt_canon) {
        const SpecifierOpt *so = &sol->opt[match_idx];
        const char *spec = so->specifier && so->specifier[0] ? so->specifier : "";

        char namestr[128] = "";
        char optval_buf[32];
        const char *optval = optval_buf;

        snprintf(namestr, sizeof(namestr), "-%s", sol->opt_canon->name);
        if (sol->opt_canon->flags & OPT_HAS_ALT) {
            const char * const *names_alt = sol->opt_canon->u1.names_alt;
            for (int i = 0; names_alt[i]; i++)
                av_strlcatf(namestr, sizeof(namestr), "/-%s", names_alt[i]);
        }

        switch (sol->type) {
        case OPT_TYPE_STRING: optval = so->u.str;                                             break;
        case OPT_TYPE_INT:    snprintf(optval_buf, sizeof(optval_buf), "%d", so->u.i);        break;
        case OPT_TYPE_INT64:  snprintf(optval_buf, sizeof(optval_buf), "%"PRId64, so->u.i64); break;
        case OPT_TYPE_FLOAT:  snprintf(optval_buf, sizeof(optval_buf), "%f", so->u.f);        break;
        case OPT_TYPE_DOUBLE: snprintf(optval_buf, sizeof(optval_buf), "%f", so->u.dbl);      break;
        default: av_assert0(0);
        }

        av_log(logctx, AV_LOG_WARNING, "Multiple %s options specified for "
               "stream %d, only the last option '-%s%s%s %s' will be used.\n",
               namestr, st->index, sol->opt_canon->name, spec[0] ? ":" : "",
               spec, optval);
    }

    return match_idx + 1;
}

#define OPT_MATCH_PER_STREAM(name, type, opt_type, m)                                   \
void opt_match_per_stream_ ## name(void *logctx, const SpecifierOptList *sol,           \
                                   AVFormatContext *fc, AVStream *st, type *out)        \
{                                                                                       \
    unsigned ret = opt_match_per_stream(logctx, opt_type, sol, fc, st);                 \
    if (ret > 0)                                                                        \
        *out = sol->opt[ret - 1].u.m;                                                   \
}

OPT_MATCH_PER_STREAM(str,   const char *, OPT_TYPE_STRING, str);
OPT_MATCH_PER_STREAM(int,   int,          OPT_TYPE_INT,    i);
OPT_MATCH_PER_STREAM(int64, int64_t,      OPT_TYPE_INT64,  i64);
OPT_MATCH_PER_STREAM(dbl,   double,       OPT_TYPE_DOUBLE, dbl);

int view_specifier_parse(const char **pspec, ViewSpecifier *vs)
{
    const char *spec = *pspec;
    char *endptr;

    vs->type = VIEW_SPECIFIER_TYPE_NONE;

    if (!strncmp(spec, "view:", 5)) {
        spec += 5;

        if (!strncmp(spec, "all", 3)) {
            spec += 3;
            vs->type = VIEW_SPECIFIER_TYPE_ALL;
        } else {
            vs->type = VIEW_SPECIFIER_TYPE_ID;
            vs->val  = strtoul(spec, &endptr, 0);
            if (endptr == spec) {
                av_log(NULL, AV_LOG_ERROR, "Invalid view ID: %s\n", spec);
                return AVERROR(EINVAL);
            }
            spec = endptr;
        }
    } else if (!strncmp(spec, "vidx:", 5)) {
        spec += 5;
        vs->type = VIEW_SPECIFIER_TYPE_IDX;
        vs->val  = strtoul(spec, &endptr, 0);
        if (endptr == spec) {
            av_log(NULL, AV_LOG_ERROR, "Invalid view index: %s\n", spec);
            return AVERROR(EINVAL);
        }
        spec = endptr;
    } else if (!strncmp(spec, "vpos:", 5)) {
        spec += 5;
        vs->type = VIEW_SPECIFIER_TYPE_POS;

        if (!strncmp(spec, "left", 4) && !cmdutils_isalnum(spec[4])) {
            spec += 4;
            vs->val = AV_STEREO3D_VIEW_LEFT;
        } else if (!strncmp(spec, "right", 5) && !cmdutils_isalnum(spec[5])) {
            spec += 5;
            vs->val = AV_STEREO3D_VIEW_RIGHT;
        } else {
            av_log(NULL, AV_LOG_ERROR, "Invalid view position: %s\n", spec);
            return AVERROR(EINVAL);
        }
    } else
        return 0;

    *pspec = spec;

    return 0;
}

int parse_and_set_vsync(const char *arg, int *vsync_var, int file_idx, int st_idx, int is_global)
{
    if      (!av_strcasecmp(arg, "cfr"))         *vsync_var = VSYNC_CFR;
    else if (!av_strcasecmp(arg, "vfr"))         *vsync_var = VSYNC_VFR;
    else if (!av_strcasecmp(arg, "passthrough")) *vsync_var = VSYNC_PASSTHROUGH;
#if FFMPEG_OPT_VSYNC_DROP
    else if (!av_strcasecmp(arg, "drop")) {
        av_log(NULL, AV_LOG_WARNING, "-vsync/fps_mode drop is deprecated\n");
        *vsync_var = VSYNC_DROP;
    }
#endif
    else if (!is_global && !av_strcasecmp(arg, "auto"))  *vsync_var = VSYNC_AUTO;
    else if (!is_global) {
        av_log(NULL, AV_LOG_FATAL, "Invalid value %s specified for fps_mode of #%d:%d.\n", arg, file_idx, st_idx);
        return AVERROR(EINVAL);
    }

#if FFMPEG_OPT_VSYNC
    if (is_global && *vsync_var == VSYNC_AUTO) {
        int ret;
        double num;

        ret = parse_number("vsync", arg, OPT_TYPE_INT, VSYNC_AUTO, VSYNC_VFR, &num);
        if (ret < 0)
            return ret;

        video_sync_method = num;
        av_log(NULL, AV_LOG_WARNING, "Passing a number to -vsync is deprecated,"
               " use a string argument as described in the manual.\n");
    }
#endif

    return 0;
}

/* Correct input file start times based on enabled streams */
static void correct_input_start_times(void)
{
    for (int i = 0; i < nb_input_files; i++) {
        InputFile       *ifile = input_files[i];
        AVFormatContext    *is = ifile->ctx;
        int64_t new_start_time = INT64_MAX, diff, abs_start_seek;

        ifile->start_time_effective = is->start_time;

        if (is->start_time == AV_NOPTS_VALUE ||
            !(is->iformat->flags & AVFMT_TS_DISCONT))
            continue;

        for (int j = 0; j < is->nb_streams; j++) {
            AVStream *st = is->streams[j];
            if(st->discard == AVDISCARD_ALL || st->start_time == AV_NOPTS_VALUE)
                continue;
            new_start_time = FFMIN(new_start_time, av_rescale_q(st->start_time, st->time_base, AV_TIME_BASE_Q));
        }

        diff = new_start_time - is->start_time;
        if (diff) {
            av_log(NULL, AV_LOG_VERBOSE, "Correcting start time of Input #%d by %"PRId64" us.\n", i, diff);
            ifile->start_time_effective = new_start_time;
            if (copy_ts && start_at_zero)
                ifile->ts_offset = -new_start_time;
            else if (!copy_ts) {
                abs_start_seek = is->start_time + ((ifile->start_time != AV_NOPTS_VALUE) ? ifile->start_time : 0);
                ifile->ts_offset = abs_start_seek > new_start_time ? -abs_start_seek : -new_start_time;
            } else if (copy_ts)
                ifile->ts_offset = 0;

            ifile->ts_offset += ifile->input_ts_offset;
        }
    }
}

static int apply_sync_offsets(void)
{
    for (int i = 0; i < nb_input_files; i++) {
        InputFile *ref, *self = input_files[i];
        int64_t adjustment;
        int64_t self_start_time, ref_start_time, self_seek_start, ref_seek_start;
        int start_times_set = 1;

        if (self->input_sync_ref == -1 || self->input_sync_ref == i) continue;
        if (self->input_sync_ref >= nb_input_files || self->input_sync_ref < -1) {
            av_log(NULL, AV_LOG_FATAL, "-isync for input %d references non-existent input %d.\n", i, self->input_sync_ref);
            return AVERROR(EINVAL);
        }

        if (copy_ts && !start_at_zero) {
            av_log(NULL, AV_LOG_FATAL, "Use of -isync requires that start_at_zero be set if copyts is set.\n");
            return AVERROR(EINVAL);
        }

        ref = input_files[self->input_sync_ref];
        if (ref->input_sync_ref != -1 && ref->input_sync_ref != self->input_sync_ref) {
            av_log(NULL, AV_LOG_ERROR, "-isync for input %d references a resynced input %d. Sync not set.\n", i, self->input_sync_ref);
            continue;
        }

        if (self->ctx->start_time_realtime != AV_NOPTS_VALUE && ref->ctx->start_time_realtime != AV_NOPTS_VALUE) {
            self_start_time = self->ctx->start_time_realtime;
            ref_start_time  =  ref->ctx->start_time_realtime;
        } else if (self->start_time_effective != AV_NOPTS_VALUE && ref->start_time_effective != AV_NOPTS_VALUE) {
            self_start_time = self->start_time_effective;
            ref_start_time  =  ref->start_time_effective;
        } else {
            start_times_set = 0;
        }

        if (start_times_set) {
            self_seek_start = self->start_time == AV_NOPTS_VALUE ? 0 : self->start_time;
            ref_seek_start  =  ref->start_time == AV_NOPTS_VALUE ? 0 :  ref->start_time;

            adjustment = (self_start_time - ref_start_time) + !copy_ts*(self_seek_start - ref_seek_start) + ref->input_ts_offset;

            self->ts_offset += adjustment;

            av_log(NULL, AV_LOG_INFO, "Adjusted ts offset for Input #%d by %"PRId64" us to sync with Input #%d.\n", i, adjustment, self->input_sync_ref);
        } else {
            av_log(NULL, AV_LOG_INFO, "Unable to identify start times for Inputs #%d and %d both. No sync adjustment made.\n", i, self->input_sync_ref);
        }
    }

    return 0;
}

static int opt_filter_threads(void *optctx, const char *opt, const char *arg)
{
    av_free(filter_nbthreads);
    filter_nbthreads = av_strdup(arg);
    return 0;
}

static int opt_abort_on(void *optctx, const char *opt, const char *arg)
{
    static const AVOption opts[] = {
        { "abort_on"           , NULL, 0, AV_OPT_TYPE_FLAGS, { .i64 = 0 }, INT64_MIN, (double)INT64_MAX,   .unit = "flags" },
        { "empty_output"       , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = ABORT_ON_FLAG_EMPTY_OUTPUT        }, .unit = "flags" },
        { "empty_output_stream", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = ABORT_ON_FLAG_EMPTY_OUTPUT_STREAM }, .unit = "flags" },
        { NULL },
    };
    static const AVClass class = {
        .class_name = "",
        .item_name  = av_default_item_name,
        .option     = opts,
        .version    = LIBAVUTIL_VERSION_INT,
    };
    const AVClass *pclass = &class;

    return av_opt_eval_flags(&pclass, &opts[0], arg, &abort_on_flags);
}

static int opt_stats_period(void *optctx, const char *opt, const char *arg)
{
    int64_t user_stats_period;
    int ret = av_parse_time(&user_stats_period, arg, 1);
    if (ret < 0)
        return ret;

    if (user_stats_period <= 0) {
        av_log(NULL, AV_LOG_ERROR, "stats_period %s must be positive.\n", arg);
        return AVERROR(EINVAL);
    }

    stats_period = user_stats_period;
    av_log(NULL, AV_LOG_INFO, "ffmpeg stats and -progress period set to %s.\n", arg);

    return 0;
}

static int opt_audio_codec(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "codec:a", arg, options);
}

static int opt_video_codec(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "codec:v", arg, options);
}

static int opt_subtitle_codec(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "codec:s", arg, options);
}

static int opt_data_codec(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "codec:d", arg, options);
}

static int opt_map(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    StreamMap *m = NULL;
    StreamSpecifier ss;
    int i, negative = 0, file_idx, disabled = 0;
    int ret, allow_unused = 0;

    memset(&ss, 0, sizeof(ss));

    if (*arg == '-') {
        negative = 1;
        arg++;
    }

    if (arg[0] == '[') {
        /* this mapping refers to lavfi output */
        const char *c = arg + 1;

        ret = GROW_ARRAY(o->stream_maps, o->nb_stream_maps);
        if (ret < 0)
            goto fail;

        m = &o->stream_maps[o->nb_stream_maps - 1];
        m->linklabel = av_get_token(&c, "]");
        if (!m->linklabel) {
            av_log(NULL, AV_LOG_ERROR, "Invalid output link label: %s.\n", arg);
            ret = AVERROR(EINVAL);
            goto fail;
        }
    } else {
        ViewSpecifier vs;
        char *endptr;

        file_idx = strtol(arg, &endptr, 0);
        if (file_idx >= nb_input_files || file_idx < 0) {
            av_log(NULL, AV_LOG_FATAL, "Invalid input file index: %d.\n", file_idx);
            ret = AVERROR(EINVAL);
            goto fail;
        }
        arg = endptr;

        ret = stream_specifier_parse(&ss, *arg == ':' ? arg + 1 : arg, 1, NULL);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Invalid stream specifier: %s\n", arg);
            goto fail;
        }

        arg = ss.remainder ? ss.remainder : "";

        ret = view_specifier_parse(&arg, &vs);
        if (ret < 0)
            goto fail;

        if (*arg) {
            if (!strcmp(arg, "?"))
                allow_unused = 1;
            else {
                av_log(NULL, AV_LOG_ERROR,
                       "Trailing garbage after stream specifier: %s\n", arg);
                ret = AVERROR(EINVAL);
                goto fail;
            }
        }

        if (negative)
            /* disable some already defined maps */
            for (i = 0; i < o->nb_stream_maps; i++) {
                m = &o->stream_maps[i];
                if (file_idx == m->file_index &&
                    stream_specifier_match(&ss,
                                           input_files[m->file_index]->ctx,
                                           input_files[m->file_index]->ctx->streams[m->stream_index],
                                           NULL))
                    m->disabled = 1;
            }
        else
            for (i = 0; i < input_files[file_idx]->nb_streams; i++) {
                if (!stream_specifier_match(&ss,
                                            input_files[file_idx]->ctx,
                                            input_files[file_idx]->ctx->streams[i],
                                            NULL))
                    continue;
                if (input_files[file_idx]->streams[i]->user_set_discard == AVDISCARD_ALL) {
                    disabled = 1;
                    continue;
                }
                ret = GROW_ARRAY(o->stream_maps, o->nb_stream_maps);
                if (ret < 0)
                    goto fail;

                m = &o->stream_maps[o->nb_stream_maps - 1];

                m->file_index   = file_idx;
                m->stream_index = i;
                m->vs           = vs;
            }
    }

    if (!m) {
        if (allow_unused) {
            av_log(NULL, AV_LOG_VERBOSE, "Stream map '%s' matches no streams; ignoring.\n", arg);
        } else if (disabled) {
            av_log(NULL, AV_LOG_FATAL, "Stream map '%s' matches disabled streams.\n"
                                       "To ignore this, add a trailing '?' to the map.\n", arg);
            ret = AVERROR(EINVAL);
            goto fail;
        } else {
            av_log(NULL, AV_LOG_FATAL, "Stream map '%s' matches no streams.\n"
                                       "To ignore this, add a trailing '?' to the map.\n", arg);
            ret = AVERROR(EINVAL);
            goto fail;
        }
    }
    ret = 0;
fail:
    stream_specifier_uninit(&ss);
    return ret;
}

static int opt_attach(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    int ret = GROW_ARRAY(o->attachments, o->nb_attachments);
    if (ret < 0)
        return ret;

    o->attachments[o->nb_attachments - 1] = av_strdup(arg);
    if (!o->attachments[o->nb_attachments - 1])
        return AVERROR(ENOMEM);

    return 0;
}

static int opt_sdp_file(void *optctx, const char *opt, const char *arg)
{
    GlobalOptionsContext *go = optctx;
    return sch_sdp_filename(go->sch, arg);
}

#if CONFIG_VAAPI
static int opt_vaapi_device(void *optctx, const char *opt, const char *arg)
{
    const char *prefix = "vaapi:";
    char *tmp;
    int err;
    tmp = av_asprintf("%s%s", prefix, arg);
    if (!tmp)
        return AVERROR(ENOMEM);
    err = hw_device_init_from_string(tmp, NULL);
    av_free(tmp);
    return err;
}
#endif

#if CONFIG_QSV
static int opt_qsv_device(void *optctx, const char *opt, const char *arg)
{
    const char *prefix = "qsv=__qsv_device:hw_any,child_device=";
    int err;
    char *tmp = av_asprintf("%s%s", prefix, arg);

    if (!tmp)
        return AVERROR(ENOMEM);

    err = hw_device_init_from_string(tmp, NULL);
    av_free(tmp);

    return err;
}
#endif

static int opt_init_hw_device(void *optctx, const char *opt, const char *arg)
{
    if (!strcmp(arg, "list")) {
        enum AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;
        printf("Supported hardware device types:\n");
        while ((type = av_hwdevice_iterate_types(type)) !=
               AV_HWDEVICE_TYPE_NONE)
            printf("%s\n", av_hwdevice_get_type_name(type));
        printf("\n");
        return AVERROR_EXIT;
    } else {
        return hw_device_init_from_string(arg, NULL);
    }
}

static int opt_filter_hw_device(void *optctx, const char *opt, const char *arg)
{
    if (filter_hw_device) {
        av_log(NULL, AV_LOG_ERROR, "Only one filter device can be used.\n");
        return AVERROR(EINVAL);
    }
    filter_hw_device = hw_device_get_by_name(arg);
    if (!filter_hw_device) {
        av_log(NULL, AV_LOG_ERROR, "Invalid filter device %s.\n", arg);
        return AVERROR(EINVAL);
    }
    return 0;
}

static int opt_recording_timestamp(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    char buf[128];
    int64_t recording_timestamp;
    int ret;
    struct tm time;

    ret = av_parse_time(&recording_timestamp, arg, 0);
    if (ret < 0)
        return ret;

    recording_timestamp /= 1e6;
    time = *gmtime((time_t*)&recording_timestamp);
    if (!strftime(buf, sizeof(buf), "creation_time=%Y-%m-%dT%H:%M:%S%z", &time))
        return -1;
    parse_option(o, "metadata", buf, options);

    av_log(NULL, AV_LOG_WARNING, "%s is deprecated, set the 'creation_time' metadata "
                                 "tag instead.\n", opt);
    return 0;
}

int find_codec(void *logctx, const char *name,
               enum AVMediaType type, int encoder, const AVCodec **pcodec)
{
    const AVCodecDescriptor *desc;
    const char *codec_string = encoder ? "encoder" : "decoder";
    const AVCodec *codec;

    codec = encoder ?
        avcodec_find_encoder_by_name(name) :
        avcodec_find_decoder_by_name(name);

    if (!codec && (desc = avcodec_descriptor_get_by_name(name))) {
        codec = encoder ? avcodec_find_encoder(desc->id) :
                          avcodec_find_decoder(desc->id);
        if (codec)
            av_log(logctx, AV_LOG_VERBOSE, "Matched %s '%s' for codec '%s'.\n",
                   codec_string, codec->name, desc->name);
    }

    if (!codec) {
        av_log(logctx, AV_LOG_FATAL, "Unknown %s '%s'\n", codec_string, name);
        return encoder ? AVERROR_ENCODER_NOT_FOUND :
                         AVERROR_DECODER_NOT_FOUND;
    }
    if (codec->type != type && !recast_media) {
        av_log(logctx, AV_LOG_FATAL, "Invalid %s type '%s'\n", codec_string, name);
        return AVERROR(EINVAL);
    }

    *pcodec = codec;
    return 0;;
}

int assert_file_overwrite(const char *filename)
{
    const char *proto_name = avio_find_protocol_name(filename);

    if (file_overwrite && no_file_overwrite) {
        fprintf(stderr, "Error, both -y and -n supplied. Exiting.\n");
        return AVERROR(EINVAL);
    }

    if (!file_overwrite) {
        if (proto_name && !strcmp(proto_name, "file") && avio_check(filename, 0) == 0) {
            if (stdin_interaction && !no_file_overwrite) {
                fprintf(stderr,"File '%s' already exists. Overwrite? [y/N] ", filename);
                fflush(stderr);
                term_exit();
                signal(SIGINT, SIG_DFL);
                if (!read_yesno()) {
                    av_log(NULL, AV_LOG_FATAL, "Not overwriting - exiting\n");
                    return AVERROR_EXIT;
                }
                term_init();
            }
            else {
                av_log(NULL, AV_LOG_FATAL, "File '%s' already exists. Exiting.\n", filename);
                return AVERROR_EXIT;
            }
        }
    }

    if (proto_name && !strcmp(proto_name, "file")) {
        for (int i = 0; i < nb_input_files; i++) {
             InputFile *file = input_files[i];
             if (file->ctx->iformat->flags & AVFMT_NOFILE)
                 continue;
             if (!strcmp(filename, file->ctx->url)) {
                 av_log(NULL, AV_LOG_FATAL, "Output %s same as Input #%d - exiting\n", filename, i);
                 av_log(NULL, AV_LOG_WARNING, "FFmpeg cannot edit existing files in-place.\n");
                 return AVERROR(EINVAL);
             }
        }
    }

    return 0;
}

/* arg format is "output-stream-index:streamid-value". */
static int opt_streamid(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    char *p;
    char idx_str[16];

    av_strlcpy(idx_str, arg, sizeof(idx_str));
    p = strchr(idx_str, ':');
    if (!p) {
        av_log(NULL, AV_LOG_FATAL,
               "Invalid value '%s' for option '%s', required syntax is 'index:value'\n",
               arg, opt);
        return AVERROR(EINVAL);
    }
    *p++ = '\0';

    return av_dict_set(&o->streamid, idx_str, p, 0);
}

static int opt_target(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    enum { PAL, NTSC, FILM, UNKNOWN } norm = UNKNOWN;
    static const char *const frame_rates[] = { "25", "30000/1001", "24000/1001" };

    if (!strncmp(arg, "pal-", 4)) {
        norm = PAL;
        arg += 4;
    } else if (!strncmp(arg, "ntsc-", 5)) {
        norm = NTSC;
        arg += 5;
    } else if (!strncmp(arg, "film-", 5)) {
        norm = FILM;
        arg += 5;
    } else {
        /* Try to determine PAL/NTSC by peeking in the input files */
        if (nb_input_files) {
            int i, j;
            for (j = 0; j < nb_input_files; j++) {
                for (i = 0; i < input_files[j]->nb_streams; i++) {
                    AVStream *st = input_files[j]->ctx->streams[i];
                    int64_t fr;
                    if (st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
                        continue;
                    fr = st->time_base.den * 1000LL / st->time_base.num;
                    if (fr == 25000) {
                        norm = PAL;
                        break;
                    } else if ((fr == 29970) || (fr == 23976)) {
                        norm = NTSC;
                        break;
                    }
                }
                if (norm != UNKNOWN)
                    break;
            }
        }
        if (norm != UNKNOWN)
            av_log(NULL, AV_LOG_INFO, "Assuming %s for target.\n", norm == PAL ? "PAL" : "NTSC");
    }

    if (norm == UNKNOWN) {
        av_log(NULL, AV_LOG_FATAL, "Could not determine norm (PAL/NTSC/NTSC-Film) for target.\n");
        av_log(NULL, AV_LOG_FATAL, "Please prefix target with \"pal-\", \"ntsc-\" or \"film-\",\n");
        av_log(NULL, AV_LOG_FATAL, "or set a framerate with \"-r xxx\".\n");
        return AVERROR(EINVAL);
    }

    if (!strcmp(arg, "vcd")) {
        opt_video_codec(o, "c:v", "mpeg1video");
        opt_audio_codec(o, "c:a", "mp2");
        parse_option(o, "f", "vcd", options);

        parse_option(o, "s", norm == PAL ? "352x288" : "352x240", options);
        parse_option(o, "r", frame_rates[norm], options);
        opt_default(NULL, "g", norm == PAL ? "15" : "18");

        opt_default(NULL, "b:v", "1150000");
        opt_default(NULL, "maxrate:v", "1150000");
        opt_default(NULL, "minrate:v", "1150000");
        opt_default(NULL, "bufsize:v", "327680"); // 40*1024*8;

        opt_default(NULL, "b:a", "224000");
        parse_option(o, "ar", "44100", options);
        parse_option(o, "ac", "2", options);

        opt_default(NULL, "packetsize", "2324");
        opt_default(NULL, "muxrate", "1411200"); // 2352 * 75 * 8;

        /* We have to offset the PTS, so that it is consistent with the SCR.
           SCR starts at 36000, but the first two packs contain only padding
           and the first pack from the other stream, respectively, may also have
           been written before.
           So the real data starts at SCR 36000+3*1200. */
        o->mux_preload = (36000 + 3 * 1200) / 90000.0; // 0.44
    } else if (!strcmp(arg, "svcd")) {

        opt_video_codec(o, "c:v", "mpeg2video");
        opt_audio_codec(o, "c:a", "mp2");
        parse_option(o, "f", "svcd", options);

        parse_option(o, "s", norm == PAL ? "480x576" : "480x480", options);
        parse_option(o, "r", frame_rates[norm], options);
        parse_option(o, "pix_fmt", "yuv420p", options);
        opt_default(NULL, "g", norm == PAL ? "15" : "18");

        opt_default(NULL, "b:v", "2040000");
        opt_default(NULL, "maxrate:v", "2516000");
        opt_default(NULL, "minrate:v", "0"); // 1145000;
        opt_default(NULL, "bufsize:v", "1835008"); // 224*1024*8;
        opt_default(NULL, "scan_offset", "1");

        opt_default(NULL, "b:a", "224000");
        parse_option(o, "ar", "44100", options);

        opt_default(NULL, "packetsize", "2324");

    } else if (!strcmp(arg, "dvd")) {

        opt_video_codec(o, "c:v", "mpeg2video");
        opt_audio_codec(o, "c:a", "ac3");
        parse_option(o, "f", "dvd", options);

        parse_option(o, "s", norm == PAL ? "720x576" : "720x480", options);
        parse_option(o, "r", frame_rates[norm], options);
        parse_option(o, "pix_fmt", "yuv420p", options);
        opt_default(NULL, "g", norm == PAL ? "15" : "18");

        opt_default(NULL, "b:v", "6000000");
        opt_default(NULL, "maxrate:v", "9000000");
        opt_default(NULL, "minrate:v", "0"); // 1500000;
        opt_default(NULL, "bufsize:v", "1835008"); // 224*1024*8;

        opt_default(NULL, "packetsize", "2048");  // from www.mpucoder.com: DVD sectors contain 2048 bytes of data, this is also the size of one pack.
        opt_default(NULL, "muxrate", "10080000"); // from mplex project: data_rate = 1260000. mux_rate = data_rate * 8

        opt_default(NULL, "b:a", "448000");
        parse_option(o, "ar", "48000", options);

    } else if (!strncmp(arg, "dv", 2)) {

        parse_option(o, "f", "dv", options);

        parse_option(o, "s", norm == PAL ? "720x576" : "720x480", options);
        parse_option(o, "pix_fmt", !strncmp(arg, "dv50", 4) ? "yuv422p" :
                          norm == PAL ? "yuv420p" : "yuv411p", options);
        parse_option(o, "r", frame_rates[norm], options);

        parse_option(o, "ar", "48000", options);
        parse_option(o, "ac", "2", options);

    } else {
        av_log(NULL, AV_LOG_ERROR, "Unknown target: %s\n", arg);
        return AVERROR(EINVAL);
    }

    av_dict_copy(&o->g->codec_opts,  codec_opts, AV_DICT_DONT_OVERWRITE);
    av_dict_copy(&o->g->format_opts, format_opts, AV_DICT_DONT_OVERWRITE);

    return 0;
}

static int opt_vstats_file(void *optctx, const char *opt, const char *arg)
{
    av_free (vstats_filename);
    vstats_filename = av_strdup (arg);
    return 0;
}

static int opt_vstats(void *optctx, const char *opt, const char *arg)
{
    char filename[40];
    time_t today2 = time(NULL);
    struct tm *today = localtime(&today2);

    if (!today) { // maybe tomorrow
        av_log(NULL, AV_LOG_FATAL, "Unable to get current time: %s\n", strerror(errno));
        return AVERROR(errno);
    }

    snprintf(filename, sizeof(filename), "vstats_%02d%02d%02d.log", today->tm_hour, today->tm_min,
             today->tm_sec);
    return opt_vstats_file(NULL, opt, filename);
}

static int opt_video_frames(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "frames:v", arg, options);
}

static int opt_audio_frames(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "frames:a", arg, options);
}

static int opt_data_frames(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "frames:d", arg, options);
}

static int opt_default_new(OptionsContext *o, const char *opt, const char *arg)
{
    int ret;
    AVDictionary *cbak = codec_opts;
    AVDictionary *fbak = format_opts;
    codec_opts = NULL;
    format_opts = NULL;

    ret = opt_default(NULL, opt, arg);

    av_dict_copy(&o->g->codec_opts , codec_opts, 0);
    av_dict_copy(&o->g->format_opts, format_opts, 0);
    av_dict_free(&codec_opts);
    av_dict_free(&format_opts);
    codec_opts = cbak;
    format_opts = fbak;

    return ret;
}

static int opt_preset(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    FILE *f=NULL;
    char filename[1000], line[1000], tmp_line[1000];
    const char *codec_name = NULL;
    int ret = 0;

    codec_name = opt_match_per_type_str(&o->codec_names, *opt);

    if (!(f = get_preset_file(filename, sizeof(filename), arg, *opt == 'f', codec_name))) {
        if(!strncmp(arg, "libx264-lossless", strlen("libx264-lossless"))){
            av_log(NULL, AV_LOG_FATAL, "Please use -preset <speed> -qp 0\n");
        }else
            av_log(NULL, AV_LOG_FATAL, "File for preset '%s' not found\n", arg);
        return AVERROR(ENOENT);
    }

    while (fgets(line, sizeof(line), f)) {
        char *key = tmp_line, *value, *endptr;

        if (strcspn(line, "#\n\r") == 0)
            continue;
        av_strlcpy(tmp_line, line, sizeof(tmp_line));
        if (!av_strtok(key,   "=",    &value) ||
            !av_strtok(value, "\r\n", &endptr)) {
            av_log(NULL, AV_LOG_FATAL, "%s: Invalid syntax: '%s'\n", filename, line);
            ret = AVERROR(EINVAL);
            goto fail;
        }
        av_log(NULL, AV_LOG_DEBUG, "ffpreset[%s]: set '%s' = '%s'\n", filename, key, value);

        if      (!strcmp(key, "acodec")) opt_audio_codec   (o, key, value);
        else if (!strcmp(key, "vcodec")) opt_video_codec   (o, key, value);
        else if (!strcmp(key, "scodec")) opt_subtitle_codec(o, key, value);
        else if (!strcmp(key, "dcodec")) opt_data_codec    (o, key, value);
        else if (opt_default_new(o, key, value) < 0) {
            av_log(NULL, AV_LOG_FATAL, "%s: Invalid option or argument: '%s', parsed as '%s' = '%s'\n",
                   filename, line, key, value);
            ret = AVERROR(EINVAL);
            goto fail;
        }
    }

fail:
    fclose(f);

    return ret;
}

static int opt_old2new(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    int ret;
    char *s = av_asprintf("%s:%c", opt + 1, *opt);
    if (!s)
        return AVERROR(ENOMEM);
    ret = parse_option(o, s, arg, options);
    av_free(s);
    return ret;
}

static int opt_bitrate(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;

    if(!strcmp(opt, "ab")){
        av_dict_set(&o->g->codec_opts, "b:a", arg, 0);
        return 0;
    } else if(!strcmp(opt, "b")){
        av_log(NULL, AV_LOG_WARNING, "Please use -b:a or -b:v, -b is ambiguous\n");
        av_dict_set(&o->g->codec_opts, "b:v", arg, 0);
        return 0;
    }
    av_dict_set(&o->g->codec_opts, opt, arg, 0);
    return 0;
}

static int opt_qscale(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    char *s;
    int ret;
    if(!strcmp(opt, "qscale")){
        av_log(NULL, AV_LOG_WARNING, "Please use -q:a or -q:v, -qscale is ambiguous\n");
        return parse_option(o, "q:v", arg, options);
    }
    s = av_asprintf("q%s", opt + 6);
    if (!s)
        return AVERROR(ENOMEM);
    ret = parse_option(o, s, arg, options);
    av_free(s);
    return ret;
}

static int opt_profile(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    if(!strcmp(opt, "profile")){
        av_log(NULL, AV_LOG_WARNING, "Please use -profile:a or -profile:v, -profile is ambiguous\n");
        av_dict_set(&o->g->codec_opts, "profile:v", arg, 0);
        return 0;
    }
    av_dict_set(&o->g->codec_opts, opt, arg, 0);
    return 0;
}

static int opt_video_filters(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "filter:v", arg, options);
}

static int opt_audio_filters(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "filter:a", arg, options);
}

#if FFMPEG_OPT_VSYNC
static int opt_vsync(void *optctx, const char *opt, const char *arg)
{
    av_log(NULL, AV_LOG_WARNING, "-vsync is deprecated. Use -fps_mode\n");
    return parse_and_set_vsync(arg, &video_sync_method, -1, -1, 1);
}
#endif

static int opt_timecode(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    int ret;
    char *tcr = av_asprintf("timecode=%s", arg);
    if (!tcr)
        return AVERROR(ENOMEM);
    ret = parse_option(o, "metadata:g", tcr, options);
    if (ret >= 0)
        ret = av_dict_set(&o->g->codec_opts, "gop_timecode", arg, 0);
    av_free(tcr);
    return ret;
}

static int opt_audio_qscale(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "q:a", arg, options);
}

static int opt_filter_complex(void *optctx, const char *opt, const char *arg)
{
    GlobalOptionsContext *go = optctx;
    char *graph_desc;
    int ret;

    graph_desc = av_strdup(arg);
    if (!graph_desc)
        return AVERROR(ENOMEM);

    ret = GROW_ARRAY(go->filtergraphs, go->nb_filtergraphs);
    if (ret < 0) {
        av_freep(&graph_desc);
        return ret;
    }
    go->filtergraphs[go->nb_filtergraphs - 1] = graph_desc;

    return 0;
}

#if FFMPEG_OPT_FILTER_SCRIPT
static int opt_filter_complex_script(void *optctx, const char *opt, const char *arg)
{
    GlobalOptionsContext *go = optctx;
    char *graph_desc;
    int ret;

    graph_desc = file_read(arg);
    if (!graph_desc)
        return AVERROR(EINVAL);

    av_log(NULL, AV_LOG_WARNING, "-%s is deprecated, use -/filter_complex %s instead\n",
           opt, arg);

    ret = GROW_ARRAY(go->filtergraphs, go->nb_filtergraphs);
    if (ret < 0) {
        av_freep(&graph_desc);
        return ret;
    }
    go->filtergraphs[go->nb_filtergraphs - 1] = graph_desc;

    return 0;
}
#endif

void show_help_default(const char *opt, const char *arg)
{
    int show_advanced = 0, show_avoptions = 0;

    if (opt && *opt) {
        if (!strcmp(opt, "long"))
            show_advanced = 1;
        else if (!strcmp(opt, "full"))
            show_advanced = show_avoptions = 1;
        else
            av_log(NULL, AV_LOG_ERROR, "Unknown help option '%s'.\n", opt);
    }

    show_usage();

    printf("Getting help:\n"
           "    -h      -- print basic options\n"
           "    -h long -- print more options\n"
           "    -h full -- print all options (including all format and codec specific options, very long)\n"
           "    -h type=name -- print all options for the named decoder/encoder/demuxer/muxer/filter/bsf/protocol\n"
           "    See man %s for detailed description of the options.\n"
           "\n"
           "Per-stream options can be followed by :<stream_spec> to apply that option to specific streams only. "
           "<stream_spec> can be a stream index, or v/a/s for video/audio/subtitle (see manual for full syntax).\n"
           "\n", program_name);

    show_help_options(options, "Print help / information / capabilities:",
                      OPT_EXIT, OPT_EXPERT);
    if (show_advanced)
        show_help_options(options, "Advanced information / capabilities:",
                          OPT_EXIT | OPT_EXPERT, 0);

    show_help_options(options, "Global options (affect whole program "
                      "instead of just one file):",
                      0, OPT_PERFILE | OPT_EXIT | OPT_EXPERT);
    if (show_advanced)
        show_help_options(options, "Advanced global options:", OPT_EXPERT,
                          OPT_PERFILE | OPT_EXIT);

    show_help_options(options, "Per-file options (input and output):",
                      OPT_PERFILE | OPT_INPUT | OPT_OUTPUT,
                      OPT_EXIT | OPT_FLAG_PERSTREAM | OPT_EXPERT |
                      OPT_VIDEO | OPT_AUDIO | OPT_SUBTITLE | OPT_DATA);
    if (show_advanced)
        show_help_options(options, "Advanced per-file options (input and output):",
                          OPT_PERFILE | OPT_INPUT | OPT_OUTPUT | OPT_EXPERT,
                          OPT_EXIT | OPT_FLAG_PERSTREAM |
                          OPT_VIDEO | OPT_AUDIO | OPT_SUBTITLE | OPT_DATA);

    show_help_options(options, "Per-file options (input-only):",
                      OPT_PERFILE | OPT_INPUT,
                      OPT_EXIT | OPT_FLAG_PERSTREAM | OPT_OUTPUT | OPT_EXPERT |
                      OPT_VIDEO | OPT_AUDIO | OPT_SUBTITLE | OPT_DATA);
    if (show_advanced)
        show_help_options(options, "Advanced per-file options (input-only):",
                          OPT_PERFILE | OPT_INPUT | OPT_EXPERT,
                          OPT_EXIT | OPT_FLAG_PERSTREAM | OPT_OUTPUT |
                          OPT_VIDEO | OPT_AUDIO | OPT_SUBTITLE | OPT_DATA);

    show_help_options(options, "Per-file options (output-only):",
                      OPT_PERFILE | OPT_OUTPUT,
                      OPT_EXIT | OPT_FLAG_PERSTREAM | OPT_INPUT | OPT_EXPERT |
                      OPT_VIDEO | OPT_AUDIO | OPT_SUBTITLE | OPT_DATA);
    if (show_advanced)
        show_help_options(options, "Advanced per-file options (output-only):",
                          OPT_PERFILE | OPT_OUTPUT | OPT_EXPERT,
                          OPT_EXIT | OPT_FLAG_PERSTREAM | OPT_INPUT |
                          OPT_VIDEO | OPT_AUDIO | OPT_SUBTITLE | OPT_DATA);

    show_help_options(options, "Per-stream options:",
                      OPT_FLAG_PERSTREAM,
                      OPT_EXIT | OPT_EXPERT |
                      OPT_VIDEO | OPT_AUDIO | OPT_SUBTITLE | OPT_DATA);
    if (show_advanced)
        show_help_options(options, "Advanced per-stream options:",
                          OPT_FLAG_PERSTREAM | OPT_EXPERT,
                          OPT_EXIT |
                          OPT_VIDEO | OPT_AUDIO | OPT_SUBTITLE | OPT_DATA);

    show_help_options(options, "Video options:",
                      OPT_VIDEO, OPT_EXPERT | OPT_AUDIO | OPT_SUBTITLE | OPT_DATA);
    if (show_advanced)
        show_help_options(options, "Advanced Video options:",
                          OPT_EXPERT | OPT_VIDEO, OPT_AUDIO | OPT_SUBTITLE | OPT_DATA);

    show_help_options(options, "Audio options:",
                      OPT_AUDIO, OPT_EXPERT | OPT_VIDEO | OPT_SUBTITLE | OPT_DATA);
    if (show_advanced)
        show_help_options(options, "Advanced Audio options:",
                          OPT_EXPERT | OPT_AUDIO, OPT_VIDEO | OPT_SUBTITLE | OPT_DATA);

    show_help_options(options, "Subtitle options:",
                      OPT_SUBTITLE, OPT_EXPERT | OPT_VIDEO | OPT_AUDIO | OPT_DATA);
    if (show_advanced)
        show_help_options(options, "Advanced Subtitle options:",
                          OPT_EXPERT | OPT_SUBTITLE, OPT_VIDEO | OPT_AUDIO | OPT_DATA);

    if (show_advanced)
        show_help_options(options, "Data stream options:",
                          OPT_DATA, OPT_VIDEO | OPT_AUDIO | OPT_SUBTITLE);
    printf("\n");

    if (show_avoptions) {
        int flags = AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_ENCODING_PARAM;
        show_help_children(avcodec_get_class(), flags);
        show_help_children(avformat_get_class(), flags);
#if CONFIG_SWSCALE
        show_help_children(sws_get_class(), flags);
#endif
#if CONFIG_SWRESAMPLE
        show_help_children(swr_get_class(), AV_OPT_FLAG_AUDIO_PARAM);
#endif
        show_help_children(avfilter_get_class(), AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM);
        show_help_children(av_bsf_get_class(), AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_BSF_PARAM);
    }
}

void show_usage(void)
{
    av_log(NULL, AV_LOG_INFO, "Universal media converter\n");
    av_log(NULL, AV_LOG_INFO, "usage: %s [options] [[infile options] -i infile]... {[outfile options] outfile}...\n", program_name);
    av_log(NULL, AV_LOG_INFO, "\n");
}

enum OptGroup {
    GROUP_OUTFILE,
    GROUP_INFILE,
    GROUP_DECODER,
};

static const OptionGroupDef groups[] = {
    [GROUP_OUTFILE] = { "output url",  NULL, OPT_OUTPUT },
    [GROUP_INFILE]  = { "input url",   "i",  OPT_INPUT },
    [GROUP_DECODER] = { "loopback decoder", "dec", OPT_DECODER },
};

static int open_files(OptionGroupList *l, const char *inout, Scheduler *sch,
                      int (*open_file)(const OptionsContext*, const char*,
                                       Scheduler*))
{
    int i, ret;

    for (i = 0; i < l->nb_groups; i++) {
        OptionGroup *g = &l->groups[i];
        OptionsContext o;

        init_options(&o);
        o.g = g;

        ret = parse_optgroup(&o, g, options);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error parsing options for %s file "
                   "%s.\n", inout, g->arg);
            uninit_options(&o);
            return ret;
        }

        av_log(NULL, AV_LOG_DEBUG, "Opening an %s file: %s.\n", inout, g->arg);
        ret = open_file(&o, g->arg, sch);
        uninit_options(&o);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error opening %s file %s.\n",
                   inout, g->arg);
            return ret;
        }
        av_log(NULL, AV_LOG_DEBUG, "Successfully opened the file.\n");
    }

    return 0;
}

int ffmpeg_parse_options(int argc, char **argv, Scheduler *sch)
{
    GlobalOptionsContext go = { .sch = sch };
    OptionParseContext octx;
    const char *errmsg = NULL;
    int ret;

    memset(&octx, 0, sizeof(octx));

    /* split the commandline into an internal representation */
    ret = split_commandline(&octx, argc, argv, options, groups,
                            FF_ARRAY_ELEMS(groups));
    if (ret < 0) {
        errmsg = "splitting the argument list";
        goto fail;
    }

    /* apply global options */
    ret = parse_optgroup(&go, &octx.global_opts, options);
    if (ret < 0) {
        errmsg = "parsing global options";
        goto fail;
    }

    /* configure terminal and setup signal handlers */
    term_init();

    /* create complex filtergraphs */
    for (int i = 0; i < go.nb_filtergraphs; i++) {
        ret = fg_create(NULL, go.filtergraphs[i], sch);
        go.filtergraphs[i] = NULL;
        if (ret < 0)
            goto fail;
    }

    /* open input files */
    ret = open_files(&octx.groups[GROUP_INFILE], "input", sch, ifile_open);
    if (ret < 0) {
        errmsg = "opening input files";
        goto fail;
    }

    /* open output files */
    ret = open_files(&octx.groups[GROUP_OUTFILE], "output", sch, of_open);
    if (ret < 0) {
        errmsg = "opening output files";
        goto fail;
    }

    /* create loopback decoders */
    ret = open_files(&octx.groups[GROUP_DECODER], "decoder", sch, dec_create);
    if (ret < 0) {
        errmsg = "creating loopback decoders";
        goto fail;
    }

    // bind unbound filtegraph inputs/outputs and check consistency
    ret = fg_finalise_bindings();
    if (ret < 0) {
        errmsg = "binding filtergraph inputs/outputs";
        goto fail;
    }

    correct_input_start_times();

    ret = apply_sync_offsets();
    if (ret < 0)
        goto fail;

fail:
    for (int i = 0; i < go.nb_filtergraphs; i++)
        av_freep(&go.filtergraphs[i]);
    av_freep(&go.filtergraphs);

    uninit_parse_context(&octx);
    if (ret < 0 && ret != AVERROR_EXIT) {
        av_log(NULL, AV_LOG_FATAL, "Error %s: %s\n",
               errmsg ? errmsg : "", av_err2str(ret));
    }
    return ret;
}

static int opt_progress(void *optctx, const char *opt, const char *arg)
{
    AVIOContext *avio = NULL;
    int ret;

    if (!strcmp(arg, "-"))
        arg = "pipe:";
    ret = avio_open2(&avio, arg, AVIO_FLAG_WRITE, &int_cb, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to open progress URL \"%s\": %s\n",
               arg, av_err2str(ret));
        return ret;
    }
    progress_avio = avio;
    return 0;
}

int opt_timelimit(void *optctx, const char *opt, const char *arg)
{
#if HAVE_SETRLIMIT
    int ret;
    double lim;
    struct rlimit rl;

    ret = parse_number(opt, arg, OPT_TYPE_INT64, 0, INT_MAX, &lim);
    if (ret < 0)
        return ret;

    rl = (struct rlimit){ lim, lim + 1 };
    if (setrlimit(RLIMIT_CPU, &rl))
        perror("setrlimit");
#else
    av_log(NULL, AV_LOG_WARNING, "-%s not implemented on this OS\n", opt);
#endif
    return 0;
}

#if FFMPEG_OPT_QPHIST
static int opt_qphist(void *optctx, const char *opt, const char *arg)
{
    av_log(NULL, AV_LOG_WARNING, "Option -%s is deprecated and has no effect\n", opt);
    return 0;
}
#endif

#if FFMPEG_OPT_ADRIFT_THRESHOLD
static int opt_adrift_threshold(void *optctx, const char *opt, const char *arg)
{
    av_log(NULL, AV_LOG_WARNING, "Option -%s is deprecated and has no effect\n", opt);
    return 0;
}
#endif

static const char *const alt_channel_layout[] = { "ch_layout", NULL};
static const char *const alt_codec[]          = { "c", "acodec", "vcodec", "scodec", "dcodec", NULL };
static const char *const alt_filter[]         = { "af", "vf", NULL };
static const char *const alt_frames[]         = { "aframes", "vframes", "dframes", NULL };
static const char *const alt_pre[]            = { "apre", "vpre", "spre", NULL};
static const char *const alt_qscale[]         = { "q", NULL};
static const char *const alt_tag[]            = { "atag", "vtag", "stag", NULL };

#define OFFSET(x) offsetof(OptionsContext, x)
const OptionDef options[] = {
    /* main options */
    CMDUTILS_COMMON_OPTIONS
    { "f",                      OPT_TYPE_STRING, OPT_OFFSET | OPT_INPUT | OPT_OUTPUT,
        { .off       = OFFSET(format) },
        "force container format (auto-detected otherwise)", "fmt" },
    { "y",                      OPT_TYPE_BOOL, 0,
        {              &file_overwrite },
        "overwrite output files" },
    { "n",                      OPT_TYPE_BOOL, 0,
        {              &no_file_overwrite },
        "never overwrite output files" },
    { "ignore_unknown",         OPT_TYPE_BOOL, OPT_EXPERT,
        {              &ignore_unknown_streams },
        "Ignore unknown stream types" },
    { "copy_unknown",           OPT_TYPE_BOOL, OPT_EXPERT,
        {              &copy_unknown_streams },
        "Copy unknown stream types" },
    { "recast_media",           OPT_TYPE_BOOL, OPT_EXPERT,
        {              &recast_media },
        "allow recasting stream type in order to force a decoder of different media type" },
    { "c",                      OPT_TYPE_STRING, OPT_PERSTREAM | OPT_INPUT | OPT_OUTPUT | OPT_DECODER | OPT_HAS_CANON,
        { .off       = OFFSET(codec_names) },
        "select encoder/decoder ('copy' to copy stream without reencoding)", "codec",
        .u1.name_canon = "codec", },
    { "codec",                  OPT_TYPE_STRING, OPT_PERSTREAM | OPT_INPUT | OPT_OUTPUT | OPT_DECODER | OPT_EXPERT | OPT_HAS_ALT,
        { .off       = OFFSET(codec_names) },
        "alias for -c (select encoder/decoder)", "codec",
        .u1.names_alt = alt_codec, },
    { "pre",                    OPT_TYPE_STRING, OPT_PERSTREAM | OPT_OUTPUT | OPT_EXPERT | OPT_HAS_ALT,
        { .off       = OFFSET(presets) },
        "preset name", "preset",
        .u1.names_alt = alt_pre, },
    { "map",                    OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_EXPERT | OPT_PERFILE | OPT_OUTPUT,
        { .func_arg = opt_map },
        "set input stream mapping",
        "[-]input_file_id[:stream_specifier][,sync_file_id[:stream_specifier]]" },
    { "map_metadata",           OPT_TYPE_STRING, OPT_SPEC | OPT_OUTPUT | OPT_EXPERT,
        { .off       = OFFSET(metadata_map) },
        "set metadata information of outfile from infile",
        "outfile[,metadata]:infile[,metadata]" },
    { "map_chapters",           OPT_TYPE_INT, OPT_EXPERT | OPT_OFFSET | OPT_OUTPUT,
        { .off = OFFSET(chapters_input_file) },
        "set chapters mapping", "input_file_index" },
    { "t",                      OPT_TYPE_TIME, OPT_OFFSET | OPT_INPUT | OPT_OUTPUT,
        { .off = OFFSET(recording_time) },
        "stop transcoding after specified duration",
        "duration" },
    { "to",                     OPT_TYPE_TIME, OPT_OFFSET | OPT_INPUT | OPT_OUTPUT,
        { .off = OFFSET(stop_time) },
        "stop transcoding after specified time is reached",
        "time_stop" },
    { "fs",                     OPT_TYPE_INT64, OPT_OFFSET | OPT_OUTPUT | OPT_EXPERT,
        { .off = OFFSET(limit_filesize) },
        "set the limit file size in bytes", "limit_size" },
    { "ss",                     OPT_TYPE_TIME, OPT_OFFSET | OPT_INPUT | OPT_OUTPUT,
        { .off = OFFSET(start_time) },
        "start transcoding at specified time", "time_off" },
    { "sseof",                  OPT_TYPE_TIME, OPT_OFFSET | OPT_INPUT | OPT_EXPERT,
        { .off = OFFSET(start_time_eof) },
        "set the start time offset relative to EOF", "time_off" },
    { "seek_timestamp",         OPT_TYPE_INT, OPT_OFFSET | OPT_INPUT | OPT_EXPERT,
        { .off = OFFSET(seek_timestamp) },
        "enable/disable seeking by timestamp with -ss" },
    { "accurate_seek",          OPT_TYPE_BOOL, OPT_OFFSET | OPT_EXPERT | OPT_INPUT,
        { .off = OFFSET(accurate_seek) },
        "enable/disable accurate seeking with -ss" },
    { "isync",                  OPT_TYPE_INT, OPT_OFFSET | OPT_EXPERT | OPT_INPUT,
        { .off = OFFSET(input_sync_ref) },
        "Indicate the input index for sync reference", "sync ref" },
    { "itsoffset",              OPT_TYPE_TIME, OPT_OFFSET | OPT_EXPERT | OPT_INPUT,
        { .off = OFFSET(input_ts_offset) },
        "set the input ts offset", "time_off" },
    { "itsscale",               OPT_TYPE_DOUBLE, OPT_PERSTREAM | OPT_EXPERT | OPT_INPUT,
        { .off = OFFSET(ts_scale) },
        "set the input ts scale", "scale" },
    { "timestamp",              OPT_TYPE_FUNC,   OPT_FUNC_ARG | OPT_PERFILE | OPT_EXPERT | OPT_OUTPUT,
        { .func_arg = opt_recording_timestamp },
        "set the recording timestamp ('now' to set the current time)", "time" },
    { "metadata",               OPT_TYPE_STRING, OPT_SPEC | OPT_OUTPUT,
        { .off = OFFSET(metadata) },
        "add metadata", "key=value" },
    { "program",                OPT_TYPE_STRING, OPT_SPEC | OPT_EXPERT | OPT_OUTPUT,
        { .off = OFFSET(program) },
        "add program with specified streams", "title=string:st=number..." },
    { "stream_group",           OPT_TYPE_STRING, OPT_SPEC | OPT_OUTPUT | OPT_EXPERT,
        { .off = OFFSET(stream_groups) },
        "add stream group with specified streams and group type-specific arguments", "id=number:st=number..." },
    { "dframes",                OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_PERFILE | OPT_EXPERT | OPT_OUTPUT | OPT_HAS_CANON,
        { .func_arg = opt_data_frames },
        "set the number of data frames to output", "number",
        .u1.name_canon = "frames" },
    { "benchmark",              OPT_TYPE_BOOL, OPT_EXPERT,
        { &do_benchmark },
        "add timings for benchmarking" },
    { "benchmark_all",          OPT_TYPE_BOOL, OPT_EXPERT,
        { &do_benchmark_all },
      "add timings for each task" },
    { "progress",               OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_EXPERT,
        { .func_arg = opt_progress },
      "write program-readable progress information", "url" },
    { "stdin",                  OPT_TYPE_BOOL, OPT_EXPERT,
        { &stdin_interaction },
      "enable or disable interaction on standard input" },
    { "timelimit",              OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_EXPERT,
        { .func_arg = opt_timelimit },
        "set max runtime in seconds in CPU user time", "limit" },
    { "dump",                   OPT_TYPE_BOOL, OPT_EXPERT,
        { &do_pkt_dump },
        "dump each input packet" },
    { "hex",                    OPT_TYPE_BOOL, OPT_EXPERT,
        { &do_hex_dump },
        "when dumping packets, also dump the payload" },
    { "re",                     OPT_TYPE_BOOL, OPT_EXPERT | OPT_OFFSET | OPT_INPUT,
        { .off = OFFSET(rate_emu) },
        "read input at native frame rate; equivalent to -readrate 1", "" },
    { "readrate",               OPT_TYPE_FLOAT, OPT_OFFSET | OPT_EXPERT | OPT_INPUT,
        { .off = OFFSET(readrate) },
        "read input at specified rate", "speed" },
    { "readrate_initial_burst", OPT_TYPE_DOUBLE, OPT_OFFSET | OPT_EXPERT | OPT_INPUT,
        { .off = OFFSET(readrate_initial_burst) },
        "The initial amount of input to burst read before imposing any readrate", "seconds" },
    { "readrate_catchup",       OPT_TYPE_FLOAT, OPT_OFFSET | OPT_EXPERT | OPT_INPUT,
        { .off = OFFSET(readrate_catchup) },
        "Temporary readrate used to catch up if an input lags behind the specified readrate", "speed" },
    { "target",                 OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_PERFILE | OPT_EXPERT | OPT_OUTPUT,
        { .func_arg = opt_target },
        "specify target file type (\"vcd\", \"svcd\", \"dvd\", \"dv\" or \"dv50\" "
        "with optional prefixes \"pal-\", \"ntsc-\" or \"film-\")", "type" },
    { "frame_drop_threshold",   OPT_TYPE_FLOAT, OPT_EXPERT,
        { &frame_drop_threshold },
        "frame drop threshold", "" },
    { "copyts",                 OPT_TYPE_BOOL, OPT_EXPERT,
        { &copy_ts },
        "copy timestamps" },
    { "start_at_zero",          OPT_TYPE_BOOL, OPT_EXPERT,
        { &start_at_zero },
        "shift input timestamps to start at 0 when using copyts" },
    { "copytb",                 OPT_TYPE_INT, OPT_EXPERT,
        { &copy_tb },
        "copy input stream time base when stream copying", "mode" },
    { "shortest",               OPT_TYPE_BOOL, OPT_EXPERT | OPT_OFFSET | OPT_OUTPUT,
        { .off = OFFSET(shortest) },
        "finish encoding within shortest input" },
    { "shortest_buf_duration",  OPT_TYPE_FLOAT, OPT_EXPERT | OPT_OFFSET | OPT_OUTPUT,
        { .off = OFFSET(shortest_buf_duration) },
        "maximum buffering duration (in seconds) for the -shortest option" },
    { "bitexact",               OPT_TYPE_BOOL, OPT_EXPERT | OPT_OFFSET | OPT_OUTPUT | OPT_INPUT,
        { .off = OFFSET(bitexact) },
        "bitexact mode" },
    { "dts_delta_threshold",    OPT_TYPE_FLOAT, OPT_EXPERT,
        { &dts_delta_threshold },
        "timestamp discontinuity delta threshold", "threshold" },
    { "dts_error_threshold",    OPT_TYPE_FLOAT, OPT_EXPERT,
        { &dts_error_threshold },
        "timestamp error delta threshold", "threshold" },
    { "xerror",                 OPT_TYPE_BOOL, OPT_EXPERT,
        { &exit_on_error },
        "exit on error", "error" },
    { "abort_on",               OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_EXPERT,
        { .func_arg = opt_abort_on },
        "abort on the specified condition flags", "flags" },
    { "copyinkf",               OPT_TYPE_BOOL, OPT_EXPERT | OPT_PERSTREAM | OPT_OUTPUT,
        { .off = OFFSET(copy_initial_nonkeyframes) },
        "copy initial non-keyframes" },
    { "copypriorss",            OPT_TYPE_INT, OPT_EXPERT | OPT_PERSTREAM | OPT_OUTPUT,
        { .off = OFFSET(copy_prior_start) },
        "copy or discard frames before start time" },
    { "frames",                 OPT_TYPE_INT64, OPT_PERSTREAM | OPT_OUTPUT | OPT_EXPERT | OPT_HAS_ALT,
        { .off = OFFSET(max_frames) },
        "set the number of frames to output", "number",
        .u1.names_alt = alt_frames, },
    { "tag",                    OPT_TYPE_STRING, OPT_PERSTREAM | OPT_EXPERT | OPT_OUTPUT | OPT_INPUT | OPT_HAS_ALT,
        { .off = OFFSET(codec_tags) },
        "force codec tag/fourcc", "fourcc/tag",
        .u1.names_alt = alt_tag, },
    { "q",                      OPT_TYPE_DOUBLE, OPT_EXPERT | OPT_PERSTREAM | OPT_OUTPUT | OPT_HAS_CANON,
        { .off = OFFSET(qscale) },
        "use fixed quality scale (VBR)", "q",
        .u1.name_canon = "qscale", },
    { "qscale",                 OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_EXPERT | OPT_PERFILE | OPT_OUTPUT | OPT_HAS_ALT,
        { .func_arg = opt_qscale },
        "use fixed quality scale (VBR)", "q",
        .u1.names_alt = alt_qscale, },
    { "profile",                OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_EXPERT | OPT_PERFILE | OPT_OUTPUT,
        { .func_arg = opt_profile },
        "set profile", "profile" },
    { "filter",                 OPT_TYPE_STRING, OPT_PERSTREAM | OPT_OUTPUT | OPT_HAS_ALT,
        { .off = OFFSET(filters) },
        "apply specified filters to audio/video", "filter_graph",
        .u1.names_alt = alt_filter, },
    { "filter_threads",         OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_EXPERT,
        { .func_arg = opt_filter_threads },
        "number of non-complex filter threads" },
#if FFMPEG_OPT_FILTER_SCRIPT
    { "filter_script",          OPT_TYPE_STRING, OPT_PERSTREAM | OPT_EXPERT | OPT_OUTPUT,
        { .off = OFFSET(filter_scripts) },
        "deprecated, use -/filter", "filename" },
#endif
    { "reinit_filter",          OPT_TYPE_INT, OPT_PERSTREAM | OPT_INPUT | OPT_EXPERT,
        { .off = OFFSET(reinit_filters) },
        "reinit filtergraph on input parameter changes", "" },
    { "drop_changed",          OPT_TYPE_INT, OPT_PERSTREAM | OPT_INPUT | OPT_EXPERT,
        { .off = OFFSET(drop_changed) },
        "drop frame instead of reiniting filtergraph on input parameter changes", "" },
    { "filter_complex",         OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_EXPERT,
        { .func_arg = opt_filter_complex },
        "create a complex filtergraph", "graph_description" },
    { "filter_complex_threads", OPT_TYPE_INT, OPT_EXPERT,
        { &filter_complex_nbthreads },
        "number of threads for -filter_complex" },
    { "lavfi",               OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_EXPERT,
        { .func_arg = opt_filter_complex },
        "create a complex filtergraph", "graph_description" },
#if FFMPEG_OPT_FILTER_SCRIPT
    { "filter_complex_script", OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_EXPERT,
        { .func_arg = opt_filter_complex_script },
        "deprecated, use -/filter_complex instead", "filename" },
#endif
    { "print_graphs",   OPT_TYPE_BOOL, 0,
        { &print_graphs },
        "print execution graph data to stderr" },
    { "print_graphs_file", OPT_TYPE_STRING, 0,
        { &print_graphs_file },
        "write execution graph data to the specified file", "filename" },
    { "print_graphs_format", OPT_TYPE_STRING, 0,
        { &print_graphs_format },
      "set the output printing format (available formats are: default, compact, csv, flat, ini, json, xml, mermaid, mermaidhtml)", "format" },
    { "auto_conversion_filters", OPT_TYPE_BOOL, OPT_EXPERT,
        { &auto_conversion_filters },
        "enable automatic conversion filters globally" },
    { "stats",               OPT_TYPE_BOOL, 0,
        { &print_stats },
        "print progress report during encoding", },
    { "stats_period",        OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_EXPERT,
        { .func_arg = opt_stats_period },
        "set the period at which ffmpeg updates stats and -progress output", "time" },
    { "attach",              OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_PERFILE | OPT_EXPERT | OPT_OUTPUT,
        { .func_arg = opt_attach },
        "add an attachment to the output file", "filename" },
    { "dump_attachment",     OPT_TYPE_STRING, OPT_SPEC | OPT_EXPERT | OPT_INPUT,
        { .off = OFFSET(dump_attachment) },
        "extract an attachment into a file", "filename" },
    { "stream_loop",         OPT_TYPE_INT, OPT_EXPERT | OPT_INPUT | OPT_OFFSET,
        { .off = OFFSET(loop) }, "set number of times input stream shall be looped", "loop count" },
    { "debug_ts",            OPT_TYPE_BOOL, OPT_EXPERT,
        { &debug_ts },
        "print timestamp debugging info" },
    { "max_error_rate",      OPT_TYPE_FLOAT, OPT_EXPERT,
        { &max_error_rate },
        "ratio of decoding errors (0.0: no errors, 1.0: 100% errors) above which ffmpeg returns an error instead of success.", "maximum error rate" },
    { "discard",             OPT_TYPE_STRING, OPT_PERSTREAM | OPT_INPUT | OPT_EXPERT,
        { .off = OFFSET(discard) },
        "discard", "" },
    { "disposition",         OPT_TYPE_STRING, OPT_PERSTREAM | OPT_OUTPUT | OPT_EXPERT,
        { .off = OFFSET(disposition) },
        "disposition", "" },
    { "thread_queue_size",   OPT_TYPE_INT,  OPT_OFFSET | OPT_EXPERT | OPT_INPUT | OPT_OUTPUT,
        { .off = OFFSET(thread_queue_size) },
        "set the maximum number of queued packets from the demuxer" },
    { "find_stream_info",    OPT_TYPE_BOOL, OPT_INPUT | OPT_EXPERT | OPT_OFFSET,
        { .off = OFFSET(find_stream_info) },
        "read and decode the streams to fill missing information with heuristics" },
    { "bits_per_raw_sample", OPT_TYPE_INT, OPT_EXPERT | OPT_PERSTREAM | OPT_OUTPUT,
        { .off = OFFSET(bits_per_raw_sample) },
        "set the number of bits per raw sample", "number" },

    { "stats_enc_pre",      OPT_TYPE_STRING, OPT_PERSTREAM | OPT_EXPERT | OPT_OUTPUT,
        { .off = OFFSET(enc_stats_pre)      },
        "write encoding stats before encoding" },
    { "stats_enc_post",     OPT_TYPE_STRING, OPT_PERSTREAM | OPT_EXPERT | OPT_OUTPUT,
        { .off = OFFSET(enc_stats_post)     },
        "write encoding stats after encoding" },
    { "stats_mux_pre",      OPT_TYPE_STRING, OPT_PERSTREAM | OPT_EXPERT | OPT_OUTPUT,
        { .off = OFFSET(mux_stats)          },
        "write packets stats before muxing" },
    { "stats_enc_pre_fmt",  OPT_TYPE_STRING, OPT_PERSTREAM | OPT_EXPERT | OPT_OUTPUT,
        { .off = OFFSET(enc_stats_pre_fmt)  },
        "format of the stats written with -stats_enc_pre" },
    { "stats_enc_post_fmt", OPT_TYPE_STRING, OPT_PERSTREAM | OPT_EXPERT | OPT_OUTPUT,
        { .off = OFFSET(enc_stats_post_fmt) },
        "format of the stats written with -stats_enc_post" },
    { "stats_mux_pre_fmt",  OPT_TYPE_STRING, OPT_PERSTREAM | OPT_EXPERT | OPT_OUTPUT,
        { .off = OFFSET(mux_stats_fmt)      },
        "format of the stats written with -stats_mux_pre" },

    /* video options */
    { "vframes",                    OPT_TYPE_FUNC,   OPT_VIDEO | OPT_FUNC_ARG | OPT_PERFILE | OPT_OUTPUT | OPT_EXPERT | OPT_HAS_CANON,
        { .func_arg = opt_video_frames },
        "set the number of video frames to output", "number",
        .u1.name_canon = "frames", },
    { "r",                          OPT_TYPE_STRING, OPT_VIDEO | OPT_PERSTREAM | OPT_INPUT | OPT_OUTPUT,
        { .off = OFFSET(frame_rates) },
        "override input framerate/convert to given output framerate (Hz value, fraction or abbreviation)", "rate" },
    { "fpsmax",                     OPT_TYPE_STRING, OPT_VIDEO | OPT_PERSTREAM | OPT_OUTPUT | OPT_EXPERT,
        { .off = OFFSET(max_frame_rates) },
        "set max frame rate (Hz value, fraction or abbreviation)", "rate" },
    { "s",                          OPT_TYPE_STRING, OPT_VIDEO | OPT_SUBTITLE | OPT_PERSTREAM | OPT_INPUT | OPT_OUTPUT,
        { .off = OFFSET(frame_sizes) },
        "set frame size (WxH or abbreviation)", "size" },
    { "aspect",                     OPT_TYPE_STRING, OPT_VIDEO | OPT_PERSTREAM | OPT_OUTPUT,
        { .off = OFFSET(frame_aspect_ratios) },
        "set aspect ratio (4:3, 16:9 or 1.3333, 1.7777)", "aspect" },
    { "pix_fmt",                    OPT_TYPE_STRING, OPT_VIDEO | OPT_EXPERT | OPT_PERSTREAM | OPT_INPUT | OPT_OUTPUT,
        { .off = OFFSET(frame_pix_fmts) },
        "set pixel format", "format" },
    { "display_rotation",           OPT_TYPE_DOUBLE, OPT_VIDEO | OPT_PERSTREAM | OPT_INPUT | OPT_EXPERT,
        { .off = OFFSET(display_rotations) },
        "set pure counter-clockwise rotation in degrees for stream(s)",
        "angle" },
    { "display_hflip",              OPT_TYPE_BOOL,   OPT_VIDEO | OPT_PERSTREAM | OPT_INPUT | OPT_EXPERT,
        { .off = OFFSET(display_hflips) },
        "set display horizontal flip for stream(s) "
        "(overrides any display rotation if it is not set)"},
    { "display_vflip",              OPT_TYPE_BOOL,   OPT_VIDEO | OPT_PERSTREAM | OPT_INPUT | OPT_EXPERT,
        { .off = OFFSET(display_vflips) },
        "set display vertical flip for stream(s) "
        "(overrides any display rotation if it is not set)"},
    { "vn",                         OPT_TYPE_BOOL,   OPT_VIDEO | OPT_OFFSET | OPT_INPUT | OPT_OUTPUT,
        { .off = OFFSET(video_disable) },
        "disable video" },
    { "rc_override",                OPT_TYPE_STRING, OPT_VIDEO | OPT_EXPERT  | OPT_PERSTREAM | OPT_OUTPUT,
        { .off = OFFSET(rc_overrides) },
        "rate control override for specific intervals", "override" },
    { "vcodec",                     OPT_TYPE_FUNC,   OPT_VIDEO | OPT_FUNC_ARG | OPT_PERFILE | OPT_INPUT | OPT_OUTPUT | OPT_HAS_CANON,
        { .func_arg = opt_video_codec },
        "alias for -c:v (select encoder/decoder for video streams)", "codec",
        .u1.name_canon = "codec", },
    { "timecode",                   OPT_TYPE_FUNC,   OPT_VIDEO | OPT_FUNC_ARG | OPT_PERFILE | OPT_OUTPUT | OPT_EXPERT,
        { .func_arg = opt_timecode },
        "set initial TimeCode value.", "hh:mm:ss[:;.]ff" },
    { "pass",                       OPT_TYPE_INT,    OPT_VIDEO | OPT_PERSTREAM | OPT_OUTPUT | OPT_EXPERT,
        { .off = OFFSET(pass) },
        "select the pass number (1 to 3)", "n" },
    { "passlogfile",                OPT_TYPE_STRING, OPT_VIDEO | OPT_EXPERT | OPT_PERSTREAM | OPT_OUTPUT,
        { .off = OFFSET(passlogfiles) },
        "select two pass log file name prefix", "prefix" },
    { "vstats",                     OPT_TYPE_FUNC,   OPT_VIDEO | OPT_EXPERT,
        { .func_arg = opt_vstats },
        "dump video coding statistics to file" },
    { "vstats_file",                OPT_TYPE_FUNC,   OPT_VIDEO | OPT_FUNC_ARG | OPT_EXPERT,
        { .func_arg = opt_vstats_file },
        "dump video coding statistics to file", "file" },
    { "vstats_version",             OPT_TYPE_INT,    OPT_VIDEO | OPT_EXPERT,
        { &vstats_version },
        "Version of the vstats format to use."},
    { "vf",                         OPT_TYPE_FUNC,   OPT_VIDEO | OPT_FUNC_ARG | OPT_PERFILE | OPT_OUTPUT | OPT_HAS_CANON,
        { .func_arg = opt_video_filters },
        "alias for -filter:v (apply filters to video streams)", "filter_graph",
        .u1.name_canon = "filter", },
    { "intra_matrix",               OPT_TYPE_STRING, OPT_VIDEO | OPT_EXPERT | OPT_PERSTREAM | OPT_OUTPUT,
        { .off = OFFSET(intra_matrices) },
        "specify intra matrix coeffs", "matrix" },
    { "inter_matrix",               OPT_TYPE_STRING, OPT_VIDEO | OPT_EXPERT | OPT_PERSTREAM | OPT_OUTPUT,
        { .off = OFFSET(inter_matrices) },
        "specify inter matrix coeffs", "matrix" },
    { "chroma_intra_matrix",        OPT_TYPE_STRING, OPT_VIDEO | OPT_EXPERT | OPT_PERSTREAM | OPT_OUTPUT,
        { .off = OFFSET(chroma_intra_matrices) },
        "specify intra matrix coeffs", "matrix" },
    { "vtag",                       OPT_TYPE_FUNC,   OPT_VIDEO | OPT_FUNC_ARG | OPT_EXPERT  | OPT_PERFILE | OPT_INPUT | OPT_OUTPUT | OPT_HAS_CANON,
        { .func_arg = opt_old2new },
        "force video tag/fourcc", "fourcc/tag",
        .u1.name_canon = "tag", },
    { "fps_mode",                   OPT_TYPE_STRING, OPT_VIDEO | OPT_EXPERT | OPT_PERSTREAM | OPT_OUTPUT,
        { .off = OFFSET(fps_mode) },
        "set framerate mode for matching video streams; overrides vsync" },
    { "force_fps",                  OPT_TYPE_BOOL,   OPT_VIDEO | OPT_EXPERT  | OPT_PERSTREAM | OPT_OUTPUT,
        { .off = OFFSET(force_fps) },
        "force the selected framerate, disable the best supported framerate selection" },
    { "streamid",                   OPT_TYPE_FUNC,   OPT_VIDEO | OPT_FUNC_ARG | OPT_EXPERT | OPT_PERFILE | OPT_OUTPUT,
        { .func_arg = opt_streamid },
        "set the value of an outfile streamid", "streamIndex:value" },
    { "force_key_frames",           OPT_TYPE_STRING, OPT_VIDEO | OPT_EXPERT | OPT_PERSTREAM | OPT_OUTPUT,
        { .off = OFFSET(forced_key_frames) },
        "force key frames at specified timestamps", "timestamps" },
    { "b",                          OPT_TYPE_FUNC,   OPT_VIDEO | OPT_FUNC_ARG | OPT_PERFILE | OPT_OUTPUT,
        { .func_arg = opt_bitrate },
        "video bitrate (please use -b:v)", "bitrate" },
    { "hwaccel",                    OPT_TYPE_STRING, OPT_VIDEO | OPT_EXPERT | OPT_PERSTREAM | OPT_INPUT,
        { .off = OFFSET(hwaccels) },
        "use HW accelerated decoding", "hwaccel name" },
    { "hwaccel_device",             OPT_TYPE_STRING, OPT_VIDEO | OPT_EXPERT | OPT_PERSTREAM | OPT_INPUT,
        { .off = OFFSET(hwaccel_devices) },
        "select a device for HW acceleration", "devicename" },
    { "hwaccel_output_format",      OPT_TYPE_STRING, OPT_VIDEO | OPT_EXPERT | OPT_PERSTREAM | OPT_INPUT,
        { .off = OFFSET(hwaccel_output_formats) },
        "select output format used with HW accelerated decoding", "format" },
    { "hwaccels",                   OPT_TYPE_FUNC,   OPT_EXIT | OPT_EXPERT,
        { .func_arg = show_hwaccels },
        "show available HW acceleration methods" },
    { "autorotate",                 OPT_TYPE_BOOL,   OPT_VIDEO | OPT_PERSTREAM | OPT_EXPERT | OPT_INPUT,
        { .off = OFFSET(autorotate) },
        "automatically insert correct rotate filters" },
    { "autoscale",                  OPT_TYPE_BOOL,   OPT_VIDEO | OPT_PERSTREAM | OPT_EXPERT | OPT_OUTPUT,
        { .off = OFFSET(autoscale) },
        "automatically insert a scale filter at the end of the filter graph" },
    { "apply_cropping",             OPT_TYPE_STRING, OPT_VIDEO | OPT_PERSTREAM | OPT_EXPERT | OPT_INPUT,
        { .off = OFFSET(apply_cropping) },
        "select the cropping to apply" },
    { "fix_sub_duration_heartbeat", OPT_TYPE_BOOL,   OPT_VIDEO | OPT_EXPERT | OPT_PERSTREAM | OPT_OUTPUT,
        { .off = OFFSET(fix_sub_duration_heartbeat) },
        "set this video output stream to be a heartbeat stream for "
        "fix_sub_duration, according to which subtitles should be split at "
        "random access points" },

    /* audio options */
    { "aframes",          OPT_TYPE_FUNC,    OPT_AUDIO | OPT_FUNC_ARG | OPT_PERFILE | OPT_OUTPUT | OPT_EXPERT | OPT_HAS_CANON,
        { .func_arg = opt_audio_frames },
        "set the number of audio frames to output", "number",
        .u1.name_canon = "frames", },
    { "aq",               OPT_TYPE_FUNC,    OPT_AUDIO | OPT_FUNC_ARG  | OPT_PERFILE | OPT_OUTPUT,
        { .func_arg = opt_audio_qscale },
        "set audio quality (codec-specific)", "quality", },
    { "ar",               OPT_TYPE_INT,     OPT_AUDIO | OPT_PERSTREAM | OPT_INPUT | OPT_OUTPUT,
        { .off = OFFSET(audio_sample_rate) },
        "set audio sampling rate (in Hz)", "rate" },
    { "ac",               OPT_TYPE_INT,     OPT_AUDIO | OPT_PERSTREAM | OPT_INPUT | OPT_OUTPUT,
        { .off = OFFSET(audio_channels) },
        "set number of audio channels", "channels" },
    { "an",               OPT_TYPE_BOOL,    OPT_AUDIO | OPT_OFFSET | OPT_INPUT | OPT_OUTPUT,
        { .off = OFFSET(audio_disable) },
        "disable audio" },
    { "acodec",           OPT_TYPE_FUNC,    OPT_AUDIO | OPT_FUNC_ARG  | OPT_PERFILE | OPT_INPUT | OPT_OUTPUT | OPT_HAS_CANON,
        { .func_arg = opt_audio_codec },
        "alias for -c:a (select encoder/decoder for audio streams)", "codec",
        .u1.name_canon = "codec", },
    { "ab",               OPT_TYPE_FUNC,    OPT_AUDIO | OPT_FUNC_ARG | OPT_PERFILE | OPT_OUTPUT,
        { .func_arg = opt_bitrate },
        "alias for -b:a (select bitrate for audio streams)", "bitrate" },
    { "apad",             OPT_TYPE_STRING,  OPT_AUDIO | OPT_PERSTREAM | OPT_EXPERT | OPT_OUTPUT,
        { .off = OFFSET(apad) },
        "audio pad", "" },
    { "atag",             OPT_TYPE_FUNC,    OPT_AUDIO | OPT_FUNC_ARG  | OPT_EXPERT | OPT_PERFILE | OPT_OUTPUT | OPT_HAS_CANON,
        { .func_arg = opt_old2new },
        "force audio tag/fourcc", "fourcc/tag",
        .u1.name_canon = "tag", },
    { "sample_fmt",       OPT_TYPE_STRING,  OPT_AUDIO | OPT_EXPERT | OPT_PERSTREAM | OPT_INPUT | OPT_OUTPUT,
        { .off = OFFSET(sample_fmts) },
        "set sample format", "format" },
    { "channel_layout",   OPT_TYPE_STRING,  OPT_AUDIO | OPT_EXPERT | OPT_PERSTREAM | OPT_INPUT | OPT_OUTPUT | OPT_HAS_ALT,
        { .off = OFFSET(audio_ch_layouts) },
        "set channel layout", "layout",
        .u1.names_alt = alt_channel_layout, },
    { "ch_layout",        OPT_TYPE_STRING,  OPT_AUDIO | OPT_EXPERT | OPT_PERSTREAM | OPT_INPUT | OPT_OUTPUT | OPT_HAS_CANON,
        { .off = OFFSET(audio_ch_layouts) },
        "set channel layout", "layout",
        .u1.name_canon = "channel_layout", },
    { "af",               OPT_TYPE_FUNC,    OPT_AUDIO | OPT_FUNC_ARG  | OPT_PERFILE | OPT_OUTPUT | OPT_HAS_CANON,
        { .func_arg = opt_audio_filters },
        "alias for -filter:a (apply filters to audio streams)", "filter_graph",
        .u1.name_canon = "filter", },
    { "guess_layout_max", OPT_TYPE_INT,     OPT_AUDIO | OPT_PERSTREAM | OPT_EXPERT | OPT_INPUT,
        { .off = OFFSET(guess_layout_max) },
      "set the maximum number of channels to try to guess the channel layout" },

    /* subtitle options */
    { "sn",     OPT_TYPE_BOOL, OPT_SUBTITLE | OPT_OFFSET | OPT_INPUT | OPT_OUTPUT,
        { .off = OFFSET(subtitle_disable) },
        "disable subtitle" },
    { "scodec", OPT_TYPE_FUNC, OPT_SUBTITLE | OPT_FUNC_ARG  | OPT_PERFILE | OPT_INPUT | OPT_OUTPUT | OPT_HAS_CANON,
        { .func_arg = opt_subtitle_codec },
        "alias for -c:s (select encoder/decoder for subtitle streams)", "codec",
        .u1.name_canon = "codec", },
    { "stag",   OPT_TYPE_FUNC, OPT_SUBTITLE | OPT_FUNC_ARG  | OPT_EXPERT  | OPT_PERFILE | OPT_OUTPUT | OPT_HAS_CANON,
        { .func_arg = opt_old2new }
        , "force subtitle tag/fourcc", "fourcc/tag",
        .u1.name_canon = "tag" },
    { "fix_sub_duration", OPT_TYPE_BOOL, OPT_EXPERT | OPT_SUBTITLE | OPT_PERSTREAM | OPT_INPUT,
        { .off = OFFSET(fix_sub_duration) },
        "fix subtitles duration" },
    { "canvas_size", OPT_TYPE_STRING, OPT_SUBTITLE | OPT_PERSTREAM | OPT_INPUT | OPT_EXPERT,
        { .off = OFFSET(canvas_sizes) },
        "set canvas size (WxH or abbreviation)", "size" },

    /* muxer options */
    { "muxdelay",   OPT_TYPE_FLOAT, OPT_EXPERT | OPT_OFFSET | OPT_OUTPUT,
        { .off = OFFSET(mux_max_delay) },
        "set the maximum demux-decode delay", "seconds" },
    { "muxpreload", OPT_TYPE_FLOAT, OPT_EXPERT | OPT_OFFSET | OPT_OUTPUT,
        { .off = OFFSET(mux_preload) },
        "set the initial demux-decode delay", "seconds" },
    { "sdp_file",   OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_EXPERT | OPT_OUTPUT,
        { .func_arg = opt_sdp_file },
        "specify a file in which to print sdp information", "file" },

    { "time_base",     OPT_TYPE_STRING, OPT_EXPERT | OPT_PERSTREAM | OPT_OUTPUT,
        { .off = OFFSET(time_bases) },
        "set the desired time base hint for output stream (1:24, 1:48000 or 0.04166, 2.0833e-5)", "ratio" },
    { "enc_time_base", OPT_TYPE_STRING, OPT_EXPERT | OPT_PERSTREAM | OPT_OUTPUT,
        { .off = OFFSET(enc_time_bases) },
        "set the desired time base for the encoder (1:24, 1:48000 or 0.04166, 2.0833e-5). "
        "two special values are defined - "
        "0 = use frame rate (video) or sample rate (audio),"
        "-1 = match source time base", "ratio" },

    { "bsf", OPT_TYPE_STRING, OPT_PERSTREAM | OPT_EXPERT | OPT_OUTPUT | OPT_INPUT,
        { .off = OFFSET(bitstream_filters) },
        "A comma-separated list of bitstream filters", "bitstream_filters", },

    { "apre", OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_AUDIO | OPT_EXPERT| OPT_PERFILE | OPT_OUTPUT | OPT_HAS_CANON,
        { .func_arg = opt_preset },
        "set the audio options to the indicated preset", "preset",
        .u1.name_canon = "pre", },
    { "vpre", OPT_TYPE_FUNC, OPT_VIDEO | OPT_FUNC_ARG | OPT_EXPERT| OPT_PERFILE | OPT_OUTPUT | OPT_HAS_CANON,
        { .func_arg = opt_preset },
        "set the video options to the indicated preset", "preset",
        .u1.name_canon = "pre", },
    { "spre", OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_SUBTITLE | OPT_EXPERT| OPT_PERFILE | OPT_OUTPUT | OPT_HAS_CANON,
        { .func_arg = opt_preset },
        "set the subtitle options to the indicated preset", "preset",
        .u1.name_canon = "pre", },
    { "fpre", OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_EXPERT| OPT_PERFILE | OPT_OUTPUT | OPT_HAS_CANON,
        { .func_arg = opt_preset },
        "set options from indicated preset file", "filename",
        .u1.name_canon = "pre", },

    { "max_muxing_queue_size", OPT_TYPE_INT, OPT_PERSTREAM | OPT_EXPERT | OPT_OUTPUT,
        { .off = OFFSET(max_muxing_queue_size) },
        "maximum number of packets that can be buffered while waiting for all streams to initialize", "packets" },
    { "muxing_queue_data_threshold", OPT_TYPE_INT, OPT_PERSTREAM | OPT_EXPERT | OPT_OUTPUT,
        { .off = OFFSET(muxing_queue_data_threshold) },
        "set the threshold after which max_muxing_queue_size is taken into account", "bytes" },

    /* data codec support */
    { "dcodec", OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_DATA | OPT_PERFILE | OPT_EXPERT | OPT_INPUT | OPT_OUTPUT | OPT_HAS_CANON,
        { .func_arg = opt_data_codec },
        "alias for -c:d (select encoder/decoder for data streams)", "codec",
        .u1.name_canon = "codec", },
    { "dn", OPT_TYPE_BOOL, OPT_DATA | OPT_OFFSET | OPT_INPUT | OPT_OUTPUT,
        { .off = OFFSET(data_disable) }, "disable data" },

#if CONFIG_VAAPI
    { "vaapi_device", OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_EXPERT,
        { .func_arg = opt_vaapi_device },
        "set VAAPI hardware device (DirectX adapter index, DRM path or X11 display name)", "device" },
#endif

#if CONFIG_QSV
    { "qsv_device", OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_EXPERT,
        { .func_arg = opt_qsv_device },
        "set QSV hardware device (DirectX adapter index, DRM path or X11 display name)", "device"},
#endif

    { "init_hw_device", OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_EXPERT,
        { .func_arg = opt_init_hw_device },
        "initialise hardware device", "args" },
    { "filter_hw_device", OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_EXPERT,
        { .func_arg = opt_filter_hw_device },
        "set hardware device used when filtering", "device" },

    // deprecated options
#if FFMPEG_OPT_ADRIFT_THRESHOLD
    { "adrift_threshold", OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_EXPERT,
        { .func_arg = opt_adrift_threshold },
        "deprecated, does nothing", "threshold" },
#endif
#if FFMPEG_OPT_TOP
    { "top", OPT_TYPE_INT,     OPT_VIDEO | OPT_EXPERT | OPT_PERSTREAM | OPT_INPUT | OPT_OUTPUT,
        { .off = OFFSET(top_field_first) },
        "deprecated, use the setfield video filter", "" },
#endif
#if FFMPEG_OPT_QPHIST
    { "qphist", OPT_TYPE_FUNC, OPT_VIDEO | OPT_EXPERT,
        { .func_arg = opt_qphist },
        "deprecated, does nothing" },
#endif
#if FFMPEG_OPT_VSYNC
    { "vsync",                  OPT_TYPE_FUNC, OPT_FUNC_ARG | OPT_EXPERT,
        { .func_arg = opt_vsync },
        "set video sync method globally; deprecated, use -fps_mode", "" },
#endif

    { NULL, },
};
