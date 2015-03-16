/*
 * Copyright (c) 2012 Stefano Sabatini
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
 * send commands filter
 */

#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/file.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"
#include "internal.h"
#include "avfiltergraph.h"
#include "audio.h"
#include "video.h"

#define COMMAND_FLAG_ENTER 1
#define COMMAND_FLAG_LEAVE 2

static inline char *make_command_flags_str(AVBPrint *pbuf, int flags)
{
    static const char * const flag_strings[] = { "enter", "leave" };
    int i, is_first = 1;

    av_bprint_init(pbuf, 0, AV_BPRINT_SIZE_AUTOMATIC);
    for (i = 0; i < FF_ARRAY_ELEMS(flag_strings); i++) {
        if (flags & 1<<i) {
            if (!is_first)
                av_bprint_chars(pbuf, '+', 1);
            av_bprintf(pbuf, "%s", flag_strings[i]);
            is_first = 0;
        }
    }

    return pbuf->str;
}

typedef struct {
    int flags;
    char *target, *command, *arg;
    int index;
} Command;

typedef struct {
    int64_t start_ts;          ///< start timestamp expressed as microseconds units
    int64_t end_ts;            ///< end   timestamp expressed as microseconds units
    int index;                 ///< unique index for these interval commands
    Command *commands;
    int   nb_commands;
    int enabled;               ///< current time detected inside this interval
} Interval;

typedef struct {
    const AVClass *class;
    Interval *intervals;
    int   nb_intervals;

    char *commands_filename;
    char *commands_str;
} SendCmdContext;

#define OFFSET(x) offsetof(SendCmdContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption options[] = {
    { "commands", "set commands", OFFSET(commands_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "c",        "set commands", OFFSET(commands_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "filename", "set commands file",  OFFSET(commands_filename), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "f",        "set commands file",  OFFSET(commands_filename), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { NULL }
};

#define SPACES " \f\t\n\r"

static void skip_comments(const char **buf)
{
    while (**buf) {
        /* skip leading spaces */
        *buf += strspn(*buf, SPACES);
        if (**buf != '#')
            break;

        (*buf)++;

        /* skip comment until the end of line */
        *buf += strcspn(*buf, "\n");
        if (**buf)
            (*buf)++;
    }
}

#define COMMAND_DELIMS " \f\t\n\r,;"

static int parse_command(Command *cmd, int cmd_count, int interval_count,
                         const char **buf, void *log_ctx)
{
    int ret;

    memset(cmd, 0, sizeof(Command));
    cmd->index = cmd_count;

    /* format: [FLAGS] target command arg */
    *buf += strspn(*buf, SPACES);

    /* parse flags */
    if (**buf == '[') {
        (*buf)++; /* skip "[" */

        while (**buf) {
            int len = strcspn(*buf, "|+]");

            if      (!strncmp(*buf, "enter", strlen("enter"))) cmd->flags |= COMMAND_FLAG_ENTER;
            else if (!strncmp(*buf, "leave", strlen("leave"))) cmd->flags |= COMMAND_FLAG_LEAVE;
            else {
                char flag_buf[64];
                av_strlcpy(flag_buf, *buf, sizeof(flag_buf));
                av_log(log_ctx, AV_LOG_ERROR,
                       "Unknown flag '%s' in interval #%d, command #%d\n",
                       flag_buf, interval_count, cmd_count);
                return AVERROR(EINVAL);
            }
            *buf += len;
            if (**buf == ']')
                break;
            if (!strspn(*buf, "+|")) {
                av_log(log_ctx, AV_LOG_ERROR,
                       "Invalid flags char '%c' in interval #%d, command #%d\n",
                       **buf, interval_count, cmd_count);
                return AVERROR(EINVAL);
            }
            if (**buf)
                (*buf)++;
        }

        if (**buf != ']') {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Missing flag terminator or extraneous data found at the end of flags "
                   "in interval #%d, command #%d\n", interval_count, cmd_count);
            return AVERROR(EINVAL);
        }
        (*buf)++; /* skip "]" */
    } else {
        cmd->flags = COMMAND_FLAG_ENTER;
    }

    *buf += strspn(*buf, SPACES);
    cmd->target = av_get_token(buf, COMMAND_DELIMS);
    if (!cmd->target || !cmd->target[0]) {
        av_log(log_ctx, AV_LOG_ERROR,
               "No target specified in interval #%d, command #%d\n",
               interval_count, cmd_count);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    *buf += strspn(*buf, SPACES);
    cmd->command = av_get_token(buf, COMMAND_DELIMS);
    if (!cmd->command || !cmd->command[0]) {
        av_log(log_ctx, AV_LOG_ERROR,
               "No command specified in interval #%d, command #%d\n",
               interval_count, cmd_count);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    *buf += strspn(*buf, SPACES);
    cmd->arg = av_get_token(buf, COMMAND_DELIMS);

    return 1;

fail:
    av_freep(&cmd->target);
    av_freep(&cmd->command);
    av_freep(&cmd->arg);
    return ret;
}

static int parse_commands(Command **cmds, int *nb_cmds, int interval_count,
                          const char **buf, void *log_ctx)
{
    int cmd_count = 0;
    int ret, n = 0;
    AVBPrint pbuf;

    *cmds = NULL;
    *nb_cmds = 0;

    while (**buf) {
        Command cmd;

        if ((ret = parse_command(&cmd, cmd_count, interval_count, buf, log_ctx)) < 0)
            return ret;
        cmd_count++;

        /* (re)allocate commands array if required */
        if (*nb_cmds == n) {
            n = FFMAX(16, 2*n); /* first allocation = 16, or double the number */
            *cmds = av_realloc_f(*cmds, n, 2*sizeof(Command));
            if (!*cmds) {
                av_log(log_ctx, AV_LOG_ERROR,
                       "Could not (re)allocate command array\n");
                return AVERROR(ENOMEM);
            }
        }

        (*cmds)[(*nb_cmds)++] = cmd;

        *buf += strspn(*buf, SPACES);
        if (**buf && **buf != ';' && **buf != ',') {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Missing separator or extraneous data found at the end of "
                   "interval #%d, in command #%d\n",
                   interval_count, cmd_count);
            av_log(log_ctx, AV_LOG_ERROR,
                   "Command was parsed as: flags:[%s] target:%s command:%s arg:%s\n",
                   make_command_flags_str(&pbuf, cmd.flags), cmd.target, cmd.command, cmd.arg);
            return AVERROR(EINVAL);
        }
        if (**buf == ';')
            break;
        if (**buf == ',')
            (*buf)++;
    }

    return 0;
}

#define DELIMS " \f\t\n\r,;"

static int parse_interval(Interval *interval, int interval_count,
                          const char **buf, void *log_ctx)
{
    char *intervalstr;
    int ret;

    *buf += strspn(*buf, SPACES);
    if (!**buf)
        return 0;

    /* reset data */
    memset(interval, 0, sizeof(Interval));
    interval->index = interval_count;

    /* format: INTERVAL COMMANDS */

    /* parse interval */
    intervalstr = av_get_token(buf, DELIMS);
    if (intervalstr && intervalstr[0]) {
        char *start, *end;

        start = av_strtok(intervalstr, "-", &end);
        if ((ret = av_parse_time(&interval->start_ts, start, 1)) < 0) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Invalid start time specification '%s' in interval #%d\n",
                   start, interval_count);
            goto end;
        }

        if (end) {
            if ((ret = av_parse_time(&interval->end_ts, end, 1)) < 0) {
                av_log(log_ctx, AV_LOG_ERROR,
                       "Invalid end time specification '%s' in interval #%d\n",
                       end, interval_count);
                goto end;
            }
        } else {
            interval->end_ts = INT64_MAX;
        }
        if (interval->end_ts < interval->start_ts) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Invalid end time '%s' in interval #%d: "
                   "cannot be lesser than start time '%s'\n",
                   end, interval_count, start);
            ret = AVERROR(EINVAL);
            goto end;
        }
    } else {
        av_log(log_ctx, AV_LOG_ERROR,
               "No interval specified for interval #%d\n", interval_count);
        ret = AVERROR(EINVAL);
        goto end;
    }

    /* parse commands */
    ret = parse_commands(&interval->commands, &interval->nb_commands,
                         interval_count, buf, log_ctx);

end:
    av_free(intervalstr);
    return ret;
}

static int parse_intervals(Interval **intervals, int *nb_intervals,
                           const char *buf, void *log_ctx)
{
    int interval_count = 0;
    int ret, n = 0;

    *intervals = NULL;
    *nb_intervals = 0;

    if (!buf)
        return 0;

    while (1) {
        Interval interval;

        skip_comments(&buf);
        if (!(*buf))
            break;

        if ((ret = parse_interval(&interval, interval_count, &buf, log_ctx)) < 0)
            return ret;

        buf += strspn(buf, SPACES);
        if (*buf) {
            if (*buf != ';') {
                av_log(log_ctx, AV_LOG_ERROR,
                       "Missing terminator or extraneous data found at the end of interval #%d\n",
                       interval_count);
                return AVERROR(EINVAL);
            }
            buf++; /* skip ';' */
        }
        interval_count++;

        /* (re)allocate commands array if required */
        if (*nb_intervals == n) {
            n = FFMAX(16, 2*n); /* first allocation = 16, or double the number */
            *intervals = av_realloc_f(*intervals, n, 2*sizeof(Interval));
            if (!*intervals) {
                av_log(log_ctx, AV_LOG_ERROR,
                       "Could not (re)allocate intervals array\n");
                return AVERROR(ENOMEM);
            }
        }

        (*intervals)[(*nb_intervals)++] = interval;
    }

    return 0;
}

static int cmp_intervals(const void *a, const void *b)
{
    const Interval *i1 = a;
    const Interval *i2 = b;
    int64_t ts_diff = i1->start_ts - i2->start_ts;
    int ret;

    ret = ts_diff > 0 ? 1 : ts_diff < 0 ? -1 : 0;
    return ret == 0 ? i1->index - i2->index : ret;
}

static av_cold int init(AVFilterContext *ctx)
{
    SendCmdContext *sendcmd = ctx->priv;
    int ret, i, j;

    if ((!!sendcmd->commands_filename + !!sendcmd->commands_str) != 1) {
        av_log(ctx, AV_LOG_ERROR,
               "One and only one of the filename or commands options must be specified\n");
        return AVERROR(EINVAL);
    }

    if (sendcmd->commands_filename) {
        uint8_t *file_buf, *buf;
        size_t file_bufsize;
        ret = av_file_map(sendcmd->commands_filename,
                          &file_buf, &file_bufsize, 0, ctx);
        if (ret < 0)
            return ret;

        /* create a 0-terminated string based on the read file */
        buf = av_malloc(file_bufsize + 1);
        if (!buf) {
            av_file_unmap(file_buf, file_bufsize);
            return AVERROR(ENOMEM);
        }
        memcpy(buf, file_buf, file_bufsize);
        buf[file_bufsize] = 0;
        av_file_unmap(file_buf, file_bufsize);
        sendcmd->commands_str = buf;
    }

    if ((ret = parse_intervals(&sendcmd->intervals, &sendcmd->nb_intervals,
                               sendcmd->commands_str, ctx)) < 0)
        return ret;

    if (sendcmd->nb_intervals == 0) {
        av_log(ctx, AV_LOG_ERROR, "No commands were specified\n");
        return AVERROR(EINVAL);
    }

    qsort(sendcmd->intervals, sendcmd->nb_intervals, sizeof(Interval), cmp_intervals);

    av_log(ctx, AV_LOG_DEBUG, "Parsed commands:\n");
    for (i = 0; i < sendcmd->nb_intervals; i++) {
        AVBPrint pbuf;
        Interval *interval = &sendcmd->intervals[i];
        av_log(ctx, AV_LOG_VERBOSE, "start_time:%f end_time:%f index:%d\n",
               (double)interval->start_ts/1000000, (double)interval->end_ts/1000000, interval->index);
        for (j = 0; j < interval->nb_commands; j++) {
            Command *cmd = &interval->commands[j];
            av_log(ctx, AV_LOG_VERBOSE,
                   "    [%s] target:%s command:%s arg:%s index:%d\n",
                   make_command_flags_str(&pbuf, cmd->flags), cmd->target, cmd->command, cmd->arg, cmd->index);
        }
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SendCmdContext *sendcmd = ctx->priv;
    int i, j;

    for (i = 0; i < sendcmd->nb_intervals; i++) {
        Interval *interval = &sendcmd->intervals[i];
        for (j = 0; j < interval->nb_commands; j++) {
            Command *cmd = &interval->commands[j];
            av_freep(&cmd->target);
            av_freep(&cmd->command);
            av_freep(&cmd->arg);
        }
        av_freep(&interval->commands);
    }
    av_freep(&sendcmd->intervals);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *ref)
{
    AVFilterContext *ctx = inlink->dst;
    SendCmdContext *sendcmd = ctx->priv;
    int64_t ts;
    int i, j, ret;

    if (ref->pts == AV_NOPTS_VALUE)
        goto end;

    ts = av_rescale_q(ref->pts, inlink->time_base, AV_TIME_BASE_Q);

#define WITHIN_INTERVAL(ts, start_ts, end_ts) ((ts) >= (start_ts) && (ts) < (end_ts))

    for (i = 0; i < sendcmd->nb_intervals; i++) {
        Interval *interval = &sendcmd->intervals[i];
        int flags = 0;

        if (!interval->enabled && WITHIN_INTERVAL(ts, interval->start_ts, interval->end_ts)) {
            flags += COMMAND_FLAG_ENTER;
            interval->enabled = 1;
        }
        if (interval->enabled && !WITHIN_INTERVAL(ts, interval->start_ts, interval->end_ts)) {
            flags += COMMAND_FLAG_LEAVE;
            interval->enabled = 0;
        }

        if (flags) {
            AVBPrint pbuf;
            av_log(ctx, AV_LOG_VERBOSE,
                   "[%s] interval #%d start_ts:%f end_ts:%f ts:%f\n",
                   make_command_flags_str(&pbuf, flags), interval->index,
                   (double)interval->start_ts/1000000, (double)interval->end_ts/1000000,
                   (double)ts/1000000);

            for (j = 0; flags && j < interval->nb_commands; j++) {
                Command *cmd = &interval->commands[j];
                char buf[1024];

                if (cmd->flags & flags) {
                    av_log(ctx, AV_LOG_VERBOSE,
                           "Processing command #%d target:%s command:%s arg:%s\n",
                           cmd->index, cmd->target, cmd->command, cmd->arg);
                    ret = avfilter_graph_send_command(inlink->graph,
                                                      cmd->target, cmd->command, cmd->arg,
                                                      buf, sizeof(buf),
                                                      AVFILTER_CMD_FLAG_ONE);
                    av_log(ctx, AV_LOG_VERBOSE,
                           "Command reply for command #%d: ret:%s res:%s\n",
                           cmd->index, av_err2str(ret), buf);
                }
            }
        }
    }

end:
    switch (inlink->type) {
    case AVMEDIA_TYPE_VIDEO:
    case AVMEDIA_TYPE_AUDIO:
        return ff_filter_frame(inlink->dst->outputs[0], ref);
    }

    return AVERROR(ENOSYS);
}

#if CONFIG_SENDCMD_FILTER

#define sendcmd_options options
AVFILTER_DEFINE_CLASS(sendcmd);

static const AVFilterPad sendcmd_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad sendcmd_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_sendcmd = {
    .name        = "sendcmd",
    .description = NULL_IF_CONFIG_SMALL("Send commands to filters."),
    .init        = init,
    .uninit      = uninit,
    .priv_size   = sizeof(SendCmdContext),
    .inputs      = sendcmd_inputs,
    .outputs     = sendcmd_outputs,
    .priv_class  = &sendcmd_class,
};

#endif

#if CONFIG_ASENDCMD_FILTER

#define asendcmd_options options
AVFILTER_DEFINE_CLASS(asendcmd);

static const AVFilterPad asendcmd_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad asendcmd_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_asendcmd = {
    .name        = "asendcmd",
    .description = NULL_IF_CONFIG_SMALL("Send commands to filters."),
    .init        = init,
    .uninit      = uninit,
    .priv_size   = sizeof(SendCmdContext),
    .inputs      = asendcmd_inputs,
    .outputs     = asendcmd_outputs,
    .priv_class  = &asendcmd_class,
};

#endif
