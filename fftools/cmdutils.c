/*
 * Various utilities for command line tools
 * Copyright (c) 2000-2003 Fabrice Bellard
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

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

/* Include only the enabled headers since some compilers (namely, Sun
   Studio) will not omit unused inline functions and create undefined
   references to libraries that are not being built. */

#include "config.h"
#include "compat/va_copy.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/display.h"
#include "libavutil/getenv_utf8.h"
#include "libavutil/libm.h"
#include "libavutil/mem.h"
#include "libavutil/parseutils.h"
#include "libavutil/eval.h"
#include "libavutil/dict.h"
#include "libavutil/opt.h"
#include "cmdutils.h"
#include "fopen_utf8.h"
#include "opt_common.h"
#ifdef _WIN32
#include <windows.h>
#include "compat/w32dlfcn.h"
#endif

AVDictionary *sws_dict;
AVDictionary *swr_opts;
AVDictionary *format_opts, *codec_opts;

int hide_banner = 0;

void uninit_opts(void)
{
    av_dict_free(&swr_opts);
    av_dict_free(&sws_dict);
    av_dict_free(&format_opts);
    av_dict_free(&codec_opts);
}

void log_callback_help(void *ptr, int level, const char *fmt, va_list vl)
{
    vfprintf(stdout, fmt, vl);
}

void init_dynload(void)
{
#if HAVE_SETDLLDIRECTORY && defined(_WIN32)
    /* Calling SetDllDirectory with the empty string (but not NULL) removes the
     * current working directory from the DLL search path as a security pre-caution. */
    SetDllDirectory("");
#endif
}

int parse_number(const char *context, const char *numstr, enum OptionType type,
                 double min, double max, double *dst)
{
    char *tail;
    const char *error;
    double d = av_strtod(numstr, &tail);
    if (*tail)
        error = "Expected number for %s but found: %s\n";
    else if (d < min || d > max)
        error = "The value for %s was %s which is not within %f - %f\n";
    else if (type == OPT_TYPE_INT64 && (int64_t)d != d)
        error = "Expected int64 for %s but found %s\n";
    else if (type == OPT_TYPE_INT && (int)d != d)
        error = "Expected int for %s but found %s\n";
    else {
        *dst = d;
        return 0;
    }

    av_log(NULL, AV_LOG_FATAL, error, context, numstr, min, max);
    return AVERROR(EINVAL);
}

void show_help_options(const OptionDef *options, const char *msg, int req_flags,
                       int rej_flags)
{
    const OptionDef *po;
    int first;

    first = 1;
    for (po = options; po->name; po++) {
        char buf[128];

        if (((po->flags & req_flags) != req_flags) ||
            (po->flags & rej_flags))
            continue;

        if (first) {
            printf("%s\n", msg);
            first = 0;
        }
        av_strlcpy(buf, po->name, sizeof(buf));

        if (po->flags & OPT_FLAG_PERSTREAM)
            av_strlcat(buf, "[:<stream_spec>]", sizeof(buf));
        else if (po->flags & OPT_FLAG_SPEC)
            av_strlcat(buf, "[:<spec>]", sizeof(buf));

        if (po->argname)
            av_strlcatf(buf, sizeof(buf), " <%s>", po->argname);

        printf("-%-17s  %s\n", buf, po->help);
    }
    printf("\n");
}

void show_help_children(const AVClass *class, int flags)
{
    void *iter = NULL;
    const AVClass *child;
    if (class->option) {
        av_opt_show2(&class, NULL, flags, 0);
        printf("\n");
    }

    while (child = av_opt_child_class_iterate(class, &iter))
        show_help_children(child, flags);
}

static const OptionDef *find_option(const OptionDef *po, const char *name)
{
    if (*name == '/')
        name++;

    while (po->name) {
        const char *end;
        if (av_strstart(name, po->name, &end) && (!*end || *end == ':'))
            break;
        po++;
    }
    return po;
}

/* _WIN32 means using the windows libc - cygwin doesn't define that
 * by default. HAVE_COMMANDLINETOARGVW is true on cygwin, while
 * it doesn't provide the actual command line via GetCommandLineW(). */
#if HAVE_COMMANDLINETOARGVW && defined(_WIN32)
#include <shellapi.h>
/* Will be leaked on exit */
static char** win32_argv_utf8 = NULL;
static int win32_argc = 0;

/**
 * Prepare command line arguments for executable.
 * For Windows - perform wide-char to UTF-8 conversion.
 * Input arguments should be main() function arguments.
 * @param argc_ptr Arguments number (including executable)
 * @param argv_ptr Arguments list.
 */
static void prepare_app_arguments(int *argc_ptr, char ***argv_ptr)
{
    char *argstr_flat;
    wchar_t **argv_w;
    int i, buffsize = 0, offset = 0;

    if (win32_argv_utf8) {
        *argc_ptr = win32_argc;
        *argv_ptr = win32_argv_utf8;
        return;
    }

    win32_argc = 0;
    argv_w = CommandLineToArgvW(GetCommandLineW(), &win32_argc);
    if (win32_argc <= 0 || !argv_w)
        return;

    /* determine the UTF-8 buffer size (including NULL-termination symbols) */
    for (i = 0; i < win32_argc; i++)
        buffsize += WideCharToMultiByte(CP_UTF8, 0, argv_w[i], -1,
                                        NULL, 0, NULL, NULL);

    win32_argv_utf8 = av_mallocz(sizeof(char *) * (win32_argc + 1) + buffsize);
    argstr_flat     = (char *)win32_argv_utf8 + sizeof(char *) * (win32_argc + 1);
    if (!win32_argv_utf8) {
        LocalFree(argv_w);
        return;
    }

    for (i = 0; i < win32_argc; i++) {
        win32_argv_utf8[i] = &argstr_flat[offset];
        offset += WideCharToMultiByte(CP_UTF8, 0, argv_w[i], -1,
                                      &argstr_flat[offset],
                                      buffsize - offset, NULL, NULL);
    }
    win32_argv_utf8[i] = NULL;
    LocalFree(argv_w);

    *argc_ptr = win32_argc;
    *argv_ptr = win32_argv_utf8;
}
#else
static inline void prepare_app_arguments(int *argc_ptr, char ***argv_ptr)
{
    /* nothing to do */
}
#endif /* HAVE_COMMANDLINETOARGVW */

static int opt_has_arg(const OptionDef *o)
{
    if (o->type == OPT_TYPE_BOOL)
        return 0;
    if (o->type == OPT_TYPE_FUNC)
        return !!(o->flags & OPT_FUNC_ARG);
    return 1;
}

static int write_option(void *optctx, const OptionDef *po, const char *opt,
                        const char *arg, const OptionDef *defs)
{
    /* new-style options contain an offset into optctx, old-style address of
     * a global var*/
    void *dst = po->flags & OPT_FLAG_OFFSET ?
                (uint8_t *)optctx + po->u.off : po->u.dst_ptr;
    char *arg_allocated = NULL;

    enum OptionType so_type = po->type;

    SpecifierOptList *sol = NULL;
    double num;
    int ret = 0;

    if (*opt == '/') {
        opt++;

        if (po->type == OPT_TYPE_BOOL) {
            av_log(NULL, AV_LOG_FATAL,
                   "Requested to load an argument from file for a bool option '%s'\n",
                   po->name);
            return AVERROR(EINVAL);
        }

        arg_allocated = file_read(arg);
        if (!arg_allocated) {
            av_log(NULL, AV_LOG_FATAL,
                   "Error reading the value for option '%s' from file: %s\n",
                   opt, arg);
            return AVERROR(EINVAL);
        }

        arg = arg_allocated;
    }

    if (po->flags & OPT_FLAG_SPEC) {
        char *p = strchr(opt, ':');
        char *str;

        sol = dst;
        ret = GROW_ARRAY(sol->opt, sol->nb_opt);
        if (ret < 0)
            goto finish;

        str = av_strdup(p ? p + 1 : "");
        if (!str) {
            ret = AVERROR(ENOMEM);
            goto finish;
        }
        sol->opt[sol->nb_opt - 1].specifier = str;

        if (po->flags & OPT_FLAG_PERSTREAM) {
            ret = stream_specifier_parse(&sol->opt[sol->nb_opt - 1].stream_spec,
                                         str, 0, NULL);
            if (ret < 0)
                goto finish;
        }

        dst = &sol->opt[sol->nb_opt - 1].u;
    }

    if (po->type == OPT_TYPE_STRING) {
        char *str;
        if (arg_allocated) {
            str           = arg_allocated;
            arg_allocated = NULL;
        } else
            str = av_strdup(arg);
        av_freep(dst);

        if (!str) {
            ret = AVERROR(ENOMEM);
            goto finish;
        }

        *(char **)dst = str;
    } else if (po->type == OPT_TYPE_BOOL || po->type == OPT_TYPE_INT) {
        ret = parse_number(opt, arg, OPT_TYPE_INT64, INT_MIN, INT_MAX, &num);
        if (ret < 0)
            goto finish;

        *(int *)dst = num;
        so_type = OPT_TYPE_INT;
    } else if (po->type == OPT_TYPE_INT64) {
        ret = parse_number(opt, arg, OPT_TYPE_INT64, INT64_MIN, (double)INT64_MAX, &num);
        if (ret < 0)
            goto finish;

        *(int64_t *)dst = num;
    } else if (po->type == OPT_TYPE_TIME) {
        ret = av_parse_time(dst, arg, 1);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Invalid duration for option %s: %s\n",
                   opt, arg);
            goto finish;
        }
        so_type = OPT_TYPE_INT64;
    } else if (po->type == OPT_TYPE_FLOAT) {
        ret = parse_number(opt, arg, OPT_TYPE_FLOAT, -INFINITY, INFINITY, &num);
        if (ret < 0)
            goto finish;

        *(float *)dst = num;
    } else if (po->type == OPT_TYPE_DOUBLE) {
        ret = parse_number(opt, arg, OPT_TYPE_DOUBLE, -INFINITY, INFINITY, &num);
        if (ret < 0)
            goto finish;

        *(double *)dst = num;
    } else {
        av_assert0(po->type == OPT_TYPE_FUNC && po->u.func_arg);

        ret = po->u.func_arg(optctx, opt, arg);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Failed to set value '%s' for option '%s': %s\n",
                   arg, opt, av_err2str(ret));
            goto finish;
        }
    }
    if (po->flags & OPT_EXIT) {
        ret = AVERROR_EXIT;
        goto finish;
    }

    if (sol) {
        sol->type = so_type;
        sol->opt_canon = (po->flags & OPT_HAS_CANON) ?
                         find_option(defs, po->u1.name_canon) : po;
    }

finish:
    av_freep(&arg_allocated);
    return ret;
}

int parse_option(void *optctx, const char *opt, const char *arg,
                 const OptionDef *options)
{
    static const OptionDef opt_avoptions = {
        .name       = "AVOption passthrough",
        .type       = OPT_TYPE_FUNC,
        .flags      = OPT_FUNC_ARG,
        .u.func_arg = opt_default,
    };

    const OptionDef *po;
    int ret;

    po = find_option(options, opt);
    if (!po->name && opt[0] == 'n' && opt[1] == 'o') {
        /* handle 'no' bool option */
        po = find_option(options, opt + 2);
        if ((po->name && po->type == OPT_TYPE_BOOL))
            arg = "0";
    } else if (po->type == OPT_TYPE_BOOL)
        arg = "1";

    if (!po->name)
        po = &opt_avoptions;
    if (!po->name) {
        av_log(NULL, AV_LOG_ERROR, "Unrecognized option '%s'\n", opt);
        return AVERROR(EINVAL);
    }
    if (opt_has_arg(po) && !arg) {
        av_log(NULL, AV_LOG_ERROR, "Missing argument for option '%s'\n", opt);
        return AVERROR(EINVAL);
    }

    ret = write_option(optctx, po, opt, arg, options);
    if (ret < 0)
        return ret;

    return opt_has_arg(po);
}

int parse_options(void *optctx, int argc, char **argv, const OptionDef *options,
                  int (*parse_arg_function)(void *, const char*))
{
    const char *opt;
    int optindex, handleoptions = 1, ret;

    /* perform system-dependent conversions for arguments list */
    prepare_app_arguments(&argc, &argv);

    /* parse options */
    optindex = 1;
    while (optindex < argc) {
        opt = argv[optindex++];

        if (handleoptions && opt[0] == '-' && opt[1] != '\0') {
            if (opt[1] == '-' && opt[2] == '\0') {
                handleoptions = 0;
                continue;
            }
            opt++;

            if ((ret = parse_option(optctx, opt, argv[optindex], options)) < 0)
                return ret;
            optindex += ret;
        } else {
            if (parse_arg_function) {
                ret = parse_arg_function(optctx, opt);
                if (ret < 0)
                    return ret;
            }
        }
    }

    return 0;
}

int parse_optgroup(void *optctx, OptionGroup *g, const OptionDef *defs)
{
    int i, ret;

    av_log(NULL, AV_LOG_DEBUG, "Parsing a group of options: %s %s.\n",
           g->group_def->name, g->arg);

    for (i = 0; i < g->nb_opts; i++) {
        Option *o = &g->opts[i];

        if (g->group_def->flags &&
            !(g->group_def->flags & o->opt->flags)) {
            av_log(NULL, AV_LOG_ERROR, "Option %s (%s) cannot be applied to "
                   "%s %s -- you are trying to apply an input option to an "
                   "output file or vice versa. Move this option before the "
                   "file it belongs to.\n", o->key, o->opt->help,
                   g->group_def->name, g->arg);
            return AVERROR(EINVAL);
        }

        av_log(NULL, AV_LOG_DEBUG, "Applying option %s (%s) with argument %s.\n",
               o->key, o->opt->help, o->val);

        ret = write_option(optctx, o->opt, o->key, o->val, defs);
        if (ret < 0)
            return ret;
    }

    av_log(NULL, AV_LOG_DEBUG, "Successfully parsed a group of options.\n");

    return 0;
}

int locate_option(int argc, char **argv, const OptionDef *options,
                  const char *optname)
{
    const OptionDef *po;
    int i;

    for (i = 1; i < argc; i++) {
        const char *cur_opt = argv[i];

        if (*cur_opt++ != '-')
            continue;

        po = find_option(options, cur_opt);
        if (!po->name && cur_opt[0] == 'n' && cur_opt[1] == 'o')
            po = find_option(options, cur_opt + 2);

        if ((!po->name && !strcmp(cur_opt, optname)) ||
             (po->name && !strcmp(optname, po->name)))
            return i;

        if (!po->name || opt_has_arg(po))
            i++;
    }
    return 0;
}

static void dump_argument(FILE *report_file, const char *a)
{
    const unsigned char *p;

    for (p = a; *p; p++)
        if (!((*p >= '+' && *p <= ':') || (*p >= '@' && *p <= 'Z') ||
              *p == '_' || (*p >= 'a' && *p <= 'z')))
            break;
    if (!*p) {
        fputs(a, report_file);
        return;
    }
    fputc('"', report_file);
    for (p = a; *p; p++) {
        if (*p == '\\' || *p == '"' || *p == '$' || *p == '`')
            fprintf(report_file, "\\%c", *p);
        else if (*p < ' ' || *p > '~')
            fprintf(report_file, "\\x%02x", *p);
        else
            fputc(*p, report_file);
    }
    fputc('"', report_file);
}

static void check_options(const OptionDef *po)
{
    while (po->name) {
        if (po->flags & OPT_PERFILE)
            av_assert0(po->flags & (OPT_INPUT | OPT_OUTPUT | OPT_DECODER));

        if (po->type == OPT_TYPE_FUNC)
            av_assert0(!(po->flags & (OPT_FLAG_OFFSET | OPT_FLAG_SPEC)));

        // OPT_FUNC_ARG can only be ser for OPT_TYPE_FUNC
        av_assert0((po->type == OPT_TYPE_FUNC) || !(po->flags & OPT_FUNC_ARG));

        po++;
    }
}

void parse_loglevel(int argc, char **argv, const OptionDef *options)
{
    int idx = locate_option(argc, argv, options, "loglevel");
    char *env;

    check_options(options);

    if (!idx)
        idx = locate_option(argc, argv, options, "v");
    if (idx && argv[idx + 1])
        opt_loglevel(NULL, "loglevel", argv[idx + 1]);
    idx = locate_option(argc, argv, options, "report");
    env = getenv_utf8("FFREPORT");
    if (env || idx) {
        FILE *report_file = NULL;
        init_report(env, &report_file);
        if (report_file) {
            int i;
            fprintf(report_file, "Command line:\n");
            for (i = 0; i < argc; i++) {
                dump_argument(report_file, argv[i]);
                fputc(i < argc - 1 ? ' ' : '\n', report_file);
            }
            fflush(report_file);
        }
    }
    freeenv_utf8(env);
    idx = locate_option(argc, argv, options, "hide_banner");
    if (idx)
        hide_banner = 1;
}

static const AVOption *opt_find(void *obj, const char *name, const char *unit,
                            int opt_flags, int search_flags)
{
    const AVOption *o = av_opt_find(obj, name, unit, opt_flags, search_flags);
    if(o && !o->flags)
        return NULL;
    return o;
}

#define FLAGS ((o->type == AV_OPT_TYPE_FLAGS && (arg[0]=='-' || arg[0]=='+')) ? AV_DICT_APPEND : 0)
int opt_default(void *optctx, const char *opt, const char *arg)
{
    const AVOption *o;
    int consumed = 0;
    char opt_stripped[128];
    const char *p;
    const AVClass *cc = avcodec_get_class(), *fc = avformat_get_class();
#if CONFIG_SWSCALE
    const AVClass *sc = sws_get_class();
#endif
#if CONFIG_SWRESAMPLE
    const AVClass *swr_class = swr_get_class();
#endif

    if (!strcmp(opt, "debug") || !strcmp(opt, "fdebug"))
        av_log_set_level(AV_LOG_DEBUG);

    if (!(p = strchr(opt, ':')))
        p = opt + strlen(opt);
    av_strlcpy(opt_stripped, opt, FFMIN(sizeof(opt_stripped), p - opt + 1));

    if ((o = opt_find(&cc, opt_stripped, NULL, 0,
                         AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ)) ||
        ((opt[0] == 'v' || opt[0] == 'a' || opt[0] == 's') &&
         (o = opt_find(&cc, opt + 1, NULL, 0, AV_OPT_SEARCH_FAKE_OBJ)))) {
        av_dict_set(&codec_opts, opt, arg, FLAGS);
        consumed = 1;
    }
    if ((o = opt_find(&fc, opt, NULL, 0,
                         AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ))) {
        av_dict_set(&format_opts, opt, arg, FLAGS);
        if (consumed)
            av_log(NULL, AV_LOG_VERBOSE, "Routing option %s to both codec and muxer layer\n", opt);
        consumed = 1;
    }
#if CONFIG_SWSCALE
    if (!consumed && (o = opt_find(&sc, opt, NULL, 0,
                         AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ))) {
        if (!strcmp(opt, "srcw") || !strcmp(opt, "srch") ||
            !strcmp(opt, "dstw") || !strcmp(opt, "dsth") ||
            !strcmp(opt, "src_format") || !strcmp(opt, "dst_format")) {
            av_log(NULL, AV_LOG_ERROR, "Directly using swscale dimensions/format options is not supported, please use the -s or -pix_fmt options\n");
            return AVERROR(EINVAL);
        }
        av_dict_set(&sws_dict, opt, arg, FLAGS);

        consumed = 1;
    }
#else
    if (!consumed && !strcmp(opt, "sws_flags")) {
        av_log(NULL, AV_LOG_WARNING, "Ignoring %s %s, due to disabled swscale\n", opt, arg);
        consumed = 1;
    }
#endif
#if CONFIG_SWRESAMPLE
    if (!consumed && (o=opt_find(&swr_class, opt, NULL, 0,
                                    AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ))) {
        av_dict_set(&swr_opts, opt, arg, FLAGS);
        consumed = 1;
    }
#endif

    if (consumed)
        return 0;
    return AVERROR_OPTION_NOT_FOUND;
}

/*
 * Check whether given option is a group separator.
 *
 * @return index of the group definition that matched or -1 if none
 */
static int match_group_separator(const OptionGroupDef *groups, int nb_groups,
                                 const char *opt)
{
    int i;

    for (i = 0; i < nb_groups; i++) {
        const OptionGroupDef *p = &groups[i];
        if (p->sep && !strcmp(p->sep, opt))
            return i;
    }

    return -1;
}

/*
 * Finish parsing an option group.
 *
 * @param group_idx which group definition should this group belong to
 * @param arg argument of the group delimiting option
 */
static int finish_group(OptionParseContext *octx, int group_idx,
                        const char *arg)
{
    OptionGroupList *l = &octx->groups[group_idx];
    OptionGroup *g;
    int ret;

    ret = GROW_ARRAY(l->groups, l->nb_groups);
    if (ret < 0)
        return ret;

    g = &l->groups[l->nb_groups - 1];

    *g             = octx->cur_group;
    g->arg         = arg;
    g->group_def   = l->group_def;
    g->sws_dict    = sws_dict;
    g->swr_opts    = swr_opts;
    g->codec_opts  = codec_opts;
    g->format_opts = format_opts;

    codec_opts  = NULL;
    format_opts = NULL;
    sws_dict    = NULL;
    swr_opts    = NULL;

    memset(&octx->cur_group, 0, sizeof(octx->cur_group));

    return ret;
}

/*
 * Add an option instance to currently parsed group.
 */
static int add_opt(OptionParseContext *octx, const OptionDef *opt,
                   const char *key, const char *val)
{
    int global = !(opt->flags & OPT_PERFILE);
    OptionGroup *g = global ? &octx->global_opts : &octx->cur_group;
    int ret;

    ret = GROW_ARRAY(g->opts, g->nb_opts);
    if (ret < 0)
        return ret;

    g->opts[g->nb_opts - 1].opt = opt;
    g->opts[g->nb_opts - 1].key = key;
    g->opts[g->nb_opts - 1].val = val;

    return 0;
}

static int init_parse_context(OptionParseContext *octx,
                              const OptionGroupDef *groups, int nb_groups)
{
    static const OptionGroupDef global_group = { "global" };
    int i;

    memset(octx, 0, sizeof(*octx));

    octx->groups    = av_calloc(nb_groups, sizeof(*octx->groups));
    if (!octx->groups)
        return AVERROR(ENOMEM);
    octx->nb_groups = nb_groups;

    for (i = 0; i < octx->nb_groups; i++)
        octx->groups[i].group_def = &groups[i];

    octx->global_opts.group_def = &global_group;
    octx->global_opts.arg       = "";

    return 0;
}

void uninit_parse_context(OptionParseContext *octx)
{
    int i, j;

    for (i = 0; i < octx->nb_groups; i++) {
        OptionGroupList *l = &octx->groups[i];

        for (j = 0; j < l->nb_groups; j++) {
            av_freep(&l->groups[j].opts);
            av_dict_free(&l->groups[j].codec_opts);
            av_dict_free(&l->groups[j].format_opts);

            av_dict_free(&l->groups[j].sws_dict);
            av_dict_free(&l->groups[j].swr_opts);
        }
        av_freep(&l->groups);
    }
    av_freep(&octx->groups);

    av_freep(&octx->cur_group.opts);
    av_freep(&octx->global_opts.opts);

    uninit_opts();
}

int split_commandline(OptionParseContext *octx, int argc, char *argv[],
                      const OptionDef *options,
                      const OptionGroupDef *groups, int nb_groups)
{
    int ret;
    int optindex = 1;
    int dashdash = -2;

    /* perform system-dependent conversions for arguments list */
    prepare_app_arguments(&argc, &argv);

    ret = init_parse_context(octx, groups, nb_groups);
    if (ret < 0)
        return ret;

    av_log(NULL, AV_LOG_DEBUG, "Splitting the commandline.\n");

    while (optindex < argc) {
        const char *opt = argv[optindex++], *arg;
        const OptionDef *po;
        int group_idx;

        av_log(NULL, AV_LOG_DEBUG, "Reading option '%s' ...", opt);

        if (opt[0] == '-' && opt[1] == '-' && !opt[2]) {
            dashdash = optindex;
            continue;
        }
        /* unnamed group separators, e.g. output filename */
        if (opt[0] != '-' || !opt[1] || dashdash+1 == optindex) {
            ret = finish_group(octx, 0, opt);
            if (ret < 0)
                return ret;

            av_log(NULL, AV_LOG_DEBUG, " matched as %s.\n", groups[0].name);
            continue;
        }
        opt++;

#define GET_ARG(arg)                                                           \
do {                                                                           \
    arg = argv[optindex++];                                                    \
    if (!arg) {                                                                \
        av_log(NULL, AV_LOG_ERROR, "Missing argument for option '%s'.\n", opt);\
        return AVERROR(EINVAL);                                                \
    }                                                                          \
} while (0)

        /* named group separators, e.g. -i */
        group_idx = match_group_separator(groups, nb_groups, opt);
        if (group_idx >= 0) {
            GET_ARG(arg);
            ret = finish_group(octx, group_idx, arg);
            if (ret < 0)
                return ret;

            av_log(NULL, AV_LOG_DEBUG, " matched as %s with argument '%s'.\n",
                   groups[group_idx].name, arg);
            continue;
        }

        /* normal options */
        po = find_option(options, opt);
        if (po->name) {
            if (po->flags & OPT_EXIT) {
                /* optional argument, e.g. -h */
                arg = argv[optindex++];
            } else if (opt_has_arg(po)) {
                GET_ARG(arg);
            } else {
                arg = "1";
            }

            ret = add_opt(octx, po, opt, arg);
            if (ret < 0)
                return ret;

            av_log(NULL, AV_LOG_DEBUG, " matched as option '%s' (%s) with "
                   "argument '%s'.\n", po->name, po->help, arg);
            continue;
        }

        /* AVOptions */
        if (argv[optindex]) {
            ret = opt_default(NULL, opt, argv[optindex]);
            if (ret >= 0) {
                av_log(NULL, AV_LOG_DEBUG, " matched as AVOption '%s' with "
                       "argument '%s'.\n", opt, argv[optindex]);
                optindex++;
                continue;
            } else if (ret != AVERROR_OPTION_NOT_FOUND) {
                av_log(NULL, AV_LOG_ERROR, "Error parsing option '%s' "
                       "with argument '%s'.\n", opt, argv[optindex]);
                return ret;
            }
        }

        /* boolean -nofoo options */
        if (opt[0] == 'n' && opt[1] == 'o' &&
            (po = find_option(options, opt + 2)) &&
            po->name && po->type == OPT_TYPE_BOOL) {
            ret = add_opt(octx, po, opt, "0");
            if (ret < 0)
                return ret;

            av_log(NULL, AV_LOG_DEBUG, " matched as option '%s' (%s) with "
                   "argument 0.\n", po->name, po->help);
            continue;
        }

        av_log(NULL, AV_LOG_ERROR, "Unrecognized option '%s'.\n", opt);
        return AVERROR_OPTION_NOT_FOUND;
    }

    if (octx->cur_group.nb_opts || codec_opts || format_opts)
        av_log(NULL, AV_LOG_WARNING, "Trailing option(s) found in the "
               "command: may be ignored.\n");

    av_log(NULL, AV_LOG_DEBUG, "Finished splitting the commandline.\n");

    return 0;
}

int read_yesno(void)
{
    int c = getchar();
    int yesno = (av_toupper(c) == 'Y');

    while (c != '\n' && c != EOF)
        c = getchar();

    return yesno;
}

FILE *get_preset_file(char *filename, size_t filename_size,
                      const char *preset_name, int is_path,
                      const char *codec_name)
{
    FILE *f = NULL;
    int i;
#if HAVE_GETMODULEHANDLE && defined(_WIN32)
    char *datadir = NULL;
#endif
    char *env_home = getenv_utf8("HOME");
    char *env_ffmpeg_datadir = getenv_utf8("FFMPEG_DATADIR");
    const char *base[3] = { env_ffmpeg_datadir,
                            env_home,   /* index=1(HOME) is special: search in a .ffmpeg subfolder */
                            FFMPEG_DATADIR, };

    if (is_path) {
        av_strlcpy(filename, preset_name, filename_size);
        f = fopen_utf8(filename, "r");
    } else {
#if HAVE_GETMODULEHANDLE && defined(_WIN32)
        wchar_t *datadir_w = get_module_filename(NULL);
        base[2] = NULL;

        if (wchartoutf8(datadir_w, &datadir))
            datadir = NULL;
        av_free(datadir_w);

        if (datadir)
        {
            char *ls;
            for (ls = datadir; *ls; ls++)
                if (*ls == '\\') *ls = '/';

            if (ls = strrchr(datadir, '/'))
            {
                ptrdiff_t datadir_len = ls - datadir;
                size_t desired_size = datadir_len + strlen("/ffpresets") + 1;
                char *new_datadir = av_realloc_array(
                    datadir, desired_size, sizeof *datadir);
                if (new_datadir) {
                    datadir = new_datadir;
                    datadir[datadir_len] = 0;
                    strncat(datadir, "/ffpresets",  desired_size - 1 - datadir_len);
                    base[2] = datadir;
                }
            }
        }
#endif
        for (i = 0; i < 3 && !f; i++) {
            if (!base[i])
                continue;
            snprintf(filename, filename_size, "%s%s/%s.ffpreset", base[i],
                     i != 1 ? "" : "/.ffmpeg", preset_name);
            f = fopen_utf8(filename, "r");
            if (!f && codec_name) {
                snprintf(filename, filename_size,
                         "%s%s/%s-%s.ffpreset",
                         base[i], i != 1 ? "" : "/.ffmpeg", codec_name,
                         preset_name);
                f = fopen_utf8(filename, "r");
            }
        }
    }

#if HAVE_GETMODULEHANDLE && defined(_WIN32)
    av_free(datadir);
#endif
    freeenv_utf8(env_ffmpeg_datadir);
    freeenv_utf8(env_home);
    return f;
}

int cmdutils_isalnum(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z');
}

void stream_specifier_uninit(StreamSpecifier *ss)
{
    av_freep(&ss->meta_key);
    av_freep(&ss->meta_val);
    av_freep(&ss->remainder);

    memset(ss, 0, sizeof(*ss));
}

int stream_specifier_parse(StreamSpecifier *ss, const char *spec,
                           int allow_remainder, void *logctx)
{
    char *endptr;
    int ret;

    memset(ss, 0, sizeof(*ss));

    ss->idx         = -1;
    ss->media_type  = AVMEDIA_TYPE_UNKNOWN;
    ss->stream_list = STREAM_LIST_ALL;

    av_log(logctx, AV_LOG_TRACE, "Parsing stream specifier: %s\n", spec);

    while (*spec) {
        if (*spec <= '9' && *spec >= '0') { /* opt:index */
            ss->idx = strtol(spec, &endptr, 0);

            av_assert0(endptr > spec);
            spec = endptr;

            av_log(logctx, AV_LOG_TRACE,
                   "Parsed index: %d; remainder: %s\n", ss->idx, spec);

            // this terminates the specifier
            break;
        } else if ((*spec == 'v' || *spec == 'a' || *spec == 's' ||
                    *spec == 'd' || *spec == 't' || *spec == 'V') &&
                   !cmdutils_isalnum(*(spec + 1))) { /* opt:[vasdtV] */
            if (ss->media_type != AVMEDIA_TYPE_UNKNOWN) {
                av_log(logctx, AV_LOG_ERROR, "Stream type specified multiple times\n");
                ret = AVERROR(EINVAL);
                goto fail;
            }

            switch (*spec++) {
            case 'v': ss->media_type = AVMEDIA_TYPE_VIDEO;      break;
            case 'a': ss->media_type = AVMEDIA_TYPE_AUDIO;      break;
            case 's': ss->media_type = AVMEDIA_TYPE_SUBTITLE;   break;
            case 'd': ss->media_type = AVMEDIA_TYPE_DATA;       break;
            case 't': ss->media_type = AVMEDIA_TYPE_ATTACHMENT; break;
            case 'V': ss->media_type = AVMEDIA_TYPE_VIDEO;
                      ss->no_apic    = 1;                       break;
            default:  av_assert0(0);
            }

            av_log(logctx, AV_LOG_TRACE, "Parsed media type: %s; remainder: %s\n",
                   av_get_media_type_string(ss->media_type), spec);
        } else if (*spec == 'g' && *(spec + 1) == ':') {
            if (ss->stream_list != STREAM_LIST_ALL)
                goto multiple_stream_lists;

            spec += 2;
            if (*spec == '#' || (*spec == 'i' && *(spec + 1) == ':')) {
                ss->stream_list = STREAM_LIST_GROUP_ID;

                spec += 1 + (*spec == 'i');
            } else
                ss->stream_list = STREAM_LIST_GROUP_IDX;

            ss->list_id = strtol(spec, &endptr, 0);
            if (spec == endptr) {
                av_log(logctx, AV_LOG_ERROR, "Expected stream group idx/ID, got: %s\n", spec);
                ret = AVERROR(EINVAL);
                goto fail;
            }
            spec = endptr;

            av_log(logctx, AV_LOG_TRACE, "Parsed stream group %s: %"PRId64"; remainder: %s\n",
                   ss->stream_list == STREAM_LIST_GROUP_ID ? "ID" : "index", ss->list_id, spec);
        } else if (*spec == 'p' && *(spec + 1) == ':') {
            if (ss->stream_list != STREAM_LIST_ALL)
                goto multiple_stream_lists;

            ss->stream_list = STREAM_LIST_PROGRAM;

            spec += 2;
            ss->list_id = strtol(spec, &endptr, 0);
            if (spec == endptr) {
                av_log(logctx, AV_LOG_ERROR, "Expected program ID, got: %s\n", spec);
                ret = AVERROR(EINVAL);
                goto fail;
            }
            spec = endptr;

            av_log(logctx, AV_LOG_TRACE,
                   "Parsed program ID: %"PRId64"; remainder: %s\n", ss->list_id, spec);
        } else if (!strncmp(spec, "disp:", 5)) {
            const AVClass *st_class = av_stream_get_class();
            const AVOption       *o = av_opt_find(&st_class, "disposition", NULL, 0, AV_OPT_SEARCH_FAKE_OBJ);
            char *disp = NULL;
            size_t len;

            av_assert0(o);

            if (ss->disposition) {
                av_log(logctx, AV_LOG_ERROR, "Multiple disposition specifiers\n");
                ret = AVERROR(EINVAL);
                goto fail;
            }

            spec += 5;

            for (len = 0; cmdutils_isalnum(spec[len]) ||
                          spec[len] == '_' || spec[len] == '+'; len++)
                continue;

            disp = av_strndup(spec, len);
            if (!disp) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            ret = av_opt_eval_flags(&st_class, o, disp, &ss->disposition);
            av_freep(&disp);
            if (ret < 0) {
                av_log(logctx, AV_LOG_ERROR, "Invalid disposition specifier\n");
                goto fail;
            }

            spec += len;

            av_log(logctx, AV_LOG_TRACE,
                   "Parsed disposition: 0x%x; remainder: %s\n", ss->disposition, spec);
        } else if (*spec == '#' ||
                   (*spec == 'i' && *(spec + 1) == ':')) {
            if (ss->stream_list != STREAM_LIST_ALL)
                goto multiple_stream_lists;

            ss->stream_list = STREAM_LIST_STREAM_ID;

            spec += 1 + (*spec == 'i');
            ss->list_id = strtol(spec, &endptr, 0);
            if (spec == endptr) {
                av_log(logctx, AV_LOG_ERROR, "Expected stream ID, got: %s\n", spec);
                ret = AVERROR(EINVAL);
                goto fail;
            }
            spec = endptr;

            av_log(logctx, AV_LOG_TRACE,
                   "Parsed stream ID: %"PRId64"; remainder: %s\n", ss->list_id, spec);

            // this terminates the specifier
            break;
        } else if (*spec == 'm' && *(spec + 1) == ':') {
            av_assert0(!ss->meta_key && !ss->meta_val);

            spec += 2;
            ss->meta_key = av_get_token(&spec, ":");
            if (!ss->meta_key) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            if (*spec == ':') {
                spec++;
                ss->meta_val = av_get_token(&spec, ":");
                if (!ss->meta_val) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
            }

            av_log(logctx, AV_LOG_TRACE,
                   "Parsed metadata: %s:%s; remainder: %s", ss->meta_key,
                   ss->meta_val ? ss->meta_val : "<any value>", spec);

            // this terminates the specifier
            break;
        } else if (*spec == 'u' && (*(spec + 1) == '\0' || *(spec + 1) == ':')) {
            ss->usable_only = 1;
            spec++;
            av_log(logctx, AV_LOG_ERROR, "Parsed 'usable only'\n");

            // this terminates the specifier
            break;
        } else
            break;

        if (*spec == ':')
            spec++;
    }

    if (*spec) {
        if (!allow_remainder) {
            av_log(logctx, AV_LOG_ERROR,
                   "Trailing garbage at the end of a stream specifier: %s\n",
                   spec);
            ret = AVERROR(EINVAL);
            goto fail;
        }

        if (*spec == ':')
            spec++;

        ss->remainder = av_strdup(spec);
        if (!ss->remainder) {
            ret = AVERROR(EINVAL);
            goto fail;
        }
    }

    return 0;

multiple_stream_lists:
    av_log(logctx, AV_LOG_ERROR,
           "Cannot combine multiple program/group designators in a "
           "single stream specifier");
    ret = AVERROR(EINVAL);

fail:
    stream_specifier_uninit(ss);
    return ret;
}

unsigned stream_specifier_match(const StreamSpecifier *ss,
                                const AVFormatContext *s, const AVStream *st,
                                void *logctx)
{
    const AVStreamGroup *g = NULL;
    const AVProgram *p = NULL;
    int start_stream = 0, nb_streams;
    int nb_matched = 0;

    switch (ss->stream_list) {
    case STREAM_LIST_STREAM_ID:
        // <n-th> stream with given ID makes no sense and should be impossible to request
        av_assert0(ss->idx < 0);
        // return early if we know for sure the stream does not match
        if (st->id != ss->list_id)
            return 0;
        start_stream = st->index;
        nb_streams   = st->index + 1;
        break;
    case STREAM_LIST_ALL:
        start_stream = ss->idx >= 0 ? 0 : st->index;
        nb_streams   = st->index + 1;
        break;
    case STREAM_LIST_PROGRAM:
        for (unsigned i = 0; i < s->nb_programs; i++) {
            if (s->programs[i]->id == ss->list_id) {
                p          = s->programs[i];
                break;
            }
        }
        if (!p) {
            av_log(logctx, AV_LOG_WARNING, "No program with ID %"PRId64" exists,"
                   " stream specifier can never match\n", ss->list_id);
            return 0;
        }
        nb_streams = p->nb_stream_indexes;
        break;
    case STREAM_LIST_GROUP_ID:
        for (unsigned i = 0; i < s->nb_stream_groups; i++) {
            if (ss->list_id == s->stream_groups[i]->id) {
                g = s->stream_groups[i];
                break;
            }
        }
        // fall-through
    case STREAM_LIST_GROUP_IDX:
        if (ss->stream_list == STREAM_LIST_GROUP_IDX &&
            ss->list_id >= 0 && ss->list_id < s->nb_stream_groups)
            g = s->stream_groups[ss->list_id];

        if (!g) {
            av_log(logctx, AV_LOG_WARNING, "No stream group with group %s %"
                   PRId64" exists, stream specifier can never match\n",
                   ss->stream_list == STREAM_LIST_GROUP_ID ? "ID" : "index",
                   ss->list_id);
            return 0;
        }
        nb_streams = g->nb_streams;
        break;
    default: av_assert0(0);
    }

    for (int i = start_stream; i < nb_streams; i++) {
        const AVStream *candidate = s->streams[g ? g->streams[i]->index :
                                               p ? p->stream_index[i]   : i];

        if (ss->media_type != AVMEDIA_TYPE_UNKNOWN &&
            (ss->media_type != candidate->codecpar->codec_type ||
             (ss->no_apic && (candidate->disposition & AV_DISPOSITION_ATTACHED_PIC))))
            continue;

        if (ss->meta_key) {
            const AVDictionaryEntry *tag = av_dict_get(candidate->metadata,
                                                       ss->meta_key, NULL, 0);

            if (!tag)
                continue;
            if (ss->meta_val && strcmp(tag->value, ss->meta_val))
                continue;
        }

        if (ss->usable_only) {
            const AVCodecParameters *par = candidate->codecpar;

            switch (par->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                if (!par->sample_rate || !par->ch_layout.nb_channels ||
                    par->format == AV_SAMPLE_FMT_NONE)
                    continue;
                break;
            case AVMEDIA_TYPE_VIDEO:
                if (!par->width || !par->height || par->format == AV_PIX_FMT_NONE)
                    continue;
                break;
            case AVMEDIA_TYPE_UNKNOWN:
                continue;
            }
        }

        if (ss->disposition &&
            (candidate->disposition & ss->disposition) != ss->disposition)
            continue;

        if (st == candidate)
            return ss->idx < 0 || ss->idx == nb_matched;

        nb_matched++;
    }

    return 0;
}

int check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec)
{
    StreamSpecifier ss;
    int ret;

    ret = stream_specifier_parse(&ss, spec, 0, NULL);
    if (ret < 0)
        return ret;

    ret = stream_specifier_match(&ss, s, st, NULL);
    stream_specifier_uninit(&ss);
    return ret;
}

int filter_codec_opts(const AVDictionary *opts, enum AVCodecID codec_id,
                      AVFormatContext *s, AVStream *st, const AVCodec *codec,
                      AVDictionary **dst, AVDictionary **opts_used)
{
    AVDictionary    *ret = NULL;
    const AVDictionaryEntry *t = NULL;
    int            flags = s->oformat ? AV_OPT_FLAG_ENCODING_PARAM
                                      : AV_OPT_FLAG_DECODING_PARAM;
    char          prefix = 0;
    const AVClass    *cc = avcodec_get_class();

    switch (st->codecpar->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        prefix  = 'v';
        flags  |= AV_OPT_FLAG_VIDEO_PARAM;
        break;
    case AVMEDIA_TYPE_AUDIO:
        prefix  = 'a';
        flags  |= AV_OPT_FLAG_AUDIO_PARAM;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        prefix  = 's';
        flags  |= AV_OPT_FLAG_SUBTITLE_PARAM;
        break;
    }

    while (t = av_dict_iterate(opts, t)) {
        const AVClass *priv_class;
        char *p = strchr(t->key, ':');
        int used = 0;

        /* check stream specification in opt name */
        if (p) {
            int err = check_stream_specifier(s, st, p + 1);
            if (err < 0) {
                av_dict_free(&ret);
                return err;
            } else if (!err)
                continue;

            *p = 0;
        }

        if (av_opt_find(&cc, t->key, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ) ||
            !codec ||
            ((priv_class = codec->priv_class) &&
             av_opt_find(&priv_class, t->key, NULL, flags,
                         AV_OPT_SEARCH_FAKE_OBJ))) {
            av_dict_set(&ret, t->key, t->value, 0);
            used = 1;
        } else if (t->key[0] == prefix &&
                 av_opt_find(&cc, t->key + 1, NULL, flags,
                             AV_OPT_SEARCH_FAKE_OBJ)) {
            av_dict_set(&ret, t->key + 1, t->value, 0);
            used = 1;
        }

        if (p)
            *p = ':';

        if (used && opts_used)
            av_dict_set(opts_used, t->key, "", 0);
    }

    *dst = ret;
    return 0;
}

int setup_find_stream_info_opts(AVFormatContext *s,
                                AVDictionary *local_codec_opts,
                                AVDictionary ***dst)
{
    int ret;
    AVDictionary **opts;

    *dst = NULL;

    if (!s->nb_streams)
        return 0;

    opts = av_calloc(s->nb_streams, sizeof(*opts));
    if (!opts)
        return AVERROR(ENOMEM);

    for (int i = 0; i < s->nb_streams; i++) {
        ret = filter_codec_opts(local_codec_opts, s->streams[i]->codecpar->codec_id,
                                s, s->streams[i], NULL, &opts[i], NULL);
        if (ret < 0)
            goto fail;
    }
    *dst = opts;
    return 0;
fail:
    for (int i = 0; i < s->nb_streams; i++)
        av_dict_free(&opts[i]);
    av_freep(&opts);
    return ret;
}

int grow_array(void **array, int elem_size, int *size, int new_size)
{
    if (new_size >= INT_MAX / elem_size) {
        av_log(NULL, AV_LOG_ERROR, "Array too big.\n");
        return AVERROR(ERANGE);
    }
    if (*size < new_size) {
        uint8_t *tmp = av_realloc_array(*array, new_size, elem_size);
        if (!tmp)
            return AVERROR(ENOMEM);
        memset(tmp + *size*elem_size, 0, (new_size-*size) * elem_size);
        *size = new_size;
        *array = tmp;
        return 0;
    }
    return 0;
}

void *allocate_array_elem(void *ptr, size_t elem_size, int *nb_elems)
{
    void *new_elem;

    if (!(new_elem = av_mallocz(elem_size)) ||
        av_dynarray_add_nofree(ptr, nb_elems, new_elem) < 0)
        return NULL;
    return new_elem;
}

double get_rotation(const int32_t *displaymatrix)
{
    double theta = 0;
    if (displaymatrix)
        theta = -round(av_display_rotation_get(displaymatrix));

    theta -= 360*floor(theta/360 + 0.9/360);

    if (fabs(theta - 90*round(theta/90)) > 2)
        av_log(NULL, AV_LOG_WARNING, "Odd rotation angle.\n"
               "If you want to help, upload a sample "
               "of this file to https://streams.videolan.org/upload/ "
               "and contact the ffmpeg-devel mailing list. (ffmpeg-devel@ffmpeg.org)");

    return theta;
}

/* read file contents into a string */
char *file_read(const char *filename)
{
    AVIOContext *pb      = NULL;
    int ret = avio_open(&pb, filename, AVIO_FLAG_READ);
    AVBPrint bprint;
    char *str;

    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error opening file %s.\n", filename);
        return NULL;
    }

    av_bprint_init(&bprint, 0, AV_BPRINT_SIZE_UNLIMITED);
    ret = avio_read_to_bprint(pb, &bprint, SIZE_MAX);
    avio_closep(&pb);
    if (ret < 0) {
        av_bprint_finalize(&bprint, NULL);
        return NULL;
    }
    ret = av_bprint_finalize(&bprint, &str);
    if (ret < 0)
        return NULL;
    return str;
}

void remove_avoptions(AVDictionary **a, AVDictionary *b)
{
    const AVDictionaryEntry *t = NULL;

    while ((t = av_dict_iterate(b, t))) {
        av_dict_set(a, t->key, NULL, AV_DICT_MATCH_CASE);
    }
}

int check_avoptions(AVDictionary *m)
{
    const AVDictionaryEntry *t = av_dict_iterate(m, NULL);
    if (t) {
        av_log(NULL, AV_LOG_FATAL, "Option %s not found.\n", t->key);
        return AVERROR_OPTION_NOT_FOUND;
    }

    return 0;
}
