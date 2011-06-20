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
#include <stdlib.h>
#include <errno.h>
#include <math.h>

/* Include only the enabled headers since some compilers (namely, Sun
   Studio) will not omit unused inline functions and create undefined
   references to libraries that are not being built. */

#include "config.h"
#include "libavformat/avformat.h"
#include "libavfilter/avfilter.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libpostproc/postprocess.h"
#include "libavutil/avstring.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/eval.h"
#include "libavutil/dict.h"
#include "libavutil/opt.h"
#include "cmdutils.h"
#include "version.h"
#if CONFIG_NETWORK
#include "libavformat/network.h"
#endif
#if HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

const char **opt_names;
const char **opt_values;
static int opt_name_count;
AVCodecContext *avcodec_opts[AVMEDIA_TYPE_NB];
AVFormatContext *avformat_opts;
struct SwsContext *sws_opts;
AVDictionary *format_opts, *video_opts, *audio_opts, *sub_opts;

static const int this_year = 2011;

void init_opts(void)
{
    int i;
    for (i = 0; i < AVMEDIA_TYPE_NB; i++)
        avcodec_opts[i] = avcodec_alloc_context2(i);
    avformat_opts = avformat_alloc_context();
#if CONFIG_SWSCALE
    sws_opts = sws_getContext(16, 16, 0, 16, 16, 0, SWS_BICUBIC, NULL, NULL, NULL);
#endif
}

void uninit_opts(void)
{
    int i;
    for (i = 0; i < AVMEDIA_TYPE_NB; i++)
        av_freep(&avcodec_opts[i]);
    av_freep(&avformat_opts->key);
    av_freep(&avformat_opts);
#if CONFIG_SWSCALE
    sws_freeContext(sws_opts);
    sws_opts = NULL;
#endif
    for (i = 0; i < opt_name_count; i++) {
        av_freep(&opt_names[i]);
        av_freep(&opt_values[i]);
    }
    av_freep(&opt_names);
    av_freep(&opt_values);
    opt_name_count = 0;
    av_dict_free(&format_opts);
    av_dict_free(&video_opts);
    av_dict_free(&audio_opts);
    av_dict_free(&sub_opts);
}

void log_callback_help(void* ptr, int level, const char* fmt, va_list vl)
{
    vfprintf(stdout, fmt, vl);
}

double parse_number_or_die(const char *context, const char *numstr, int type, double min, double max)
{
    char *tail;
    const char *error;
    double d = av_strtod(numstr, &tail);
    if (*tail)
        error= "Expected number for %s but found: %s\n";
    else if (d < min || d > max)
        error= "The value for %s was %s which is not within %f - %f\n";
    else if(type == OPT_INT64 && (int64_t)d != d)
        error= "Expected int64 for %s but found %s\n";
    else if (type == OPT_INT && (int)d != d)
        error= "Expected int for %s but found %s\n";
    else
        return d;
    fprintf(stderr, error, context, numstr, min, max);
    exit(1);
}

int64_t parse_time_or_die(const char *context, const char *timestr, int is_duration)
{
    int64_t us;
    if (av_parse_time(&us, timestr, is_duration) < 0) {
        fprintf(stderr, "Invalid %s specification for %s: %s\n",
                is_duration ? "duration" : "date", context, timestr);
        exit(1);
    }
    return us;
}

void show_help_options(const OptionDef *options, const char *msg, int mask, int value)
{
    const OptionDef *po;
    int first;

    first = 1;
    for(po = options; po->name != NULL; po++) {
        char buf[64];
        if ((po->flags & mask) == value) {
            if (first) {
                printf("%s", msg);
                first = 0;
            }
            av_strlcpy(buf, po->name, sizeof(buf));
            if (po->flags & HAS_ARG) {
                av_strlcat(buf, " ", sizeof(buf));
                av_strlcat(buf, po->argname, sizeof(buf));
            }
            printf("-%-17s  %s\n", buf, po->help);
        }
    }
}

static const OptionDef* find_option(const OptionDef *po, const char *name){
    while (po->name != NULL) {
        if (!strcmp(name, po->name))
            break;
        po++;
    }
    return po;
}

#if defined(_WIN32) && !defined(__MINGW32CE__)
#include <windows.h>
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

    win32_argv_utf8 = av_mallocz(sizeof(char*) * (win32_argc + 1) + buffsize);
    argstr_flat     = (char*)win32_argv_utf8 + sizeof(char*) * (win32_argc + 1);
    if (win32_argv_utf8 == NULL) {
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
#endif /* WIN32 && !__MINGW32CE__ */

void parse_options(int argc, char **argv, const OptionDef *options,
                   int (* parse_arg_function)(const char *opt, const char *arg))
{
    const char *opt, *arg;
    int optindex, handleoptions=1;
    const OptionDef *po;

    /* perform system-dependent conversions for arguments list */
    prepare_app_arguments(&argc, &argv);

    /* parse options */
    optindex = 1;
    while (optindex < argc) {
        opt = argv[optindex++];

        if (handleoptions && opt[0] == '-' && opt[1] != '\0') {
            int bool_val = 1;
            if (opt[1] == '-' && opt[2] == '\0') {
                handleoptions = 0;
                continue;
            }
            opt++;
            po= find_option(options, opt);
            if (!po->name && opt[0] == 'n' && opt[1] == 'o') {
                /* handle 'no' bool option */
                po = find_option(options, opt + 2);
                if (!(po->name && (po->flags & OPT_BOOL)))
                    goto unknown_opt;
                bool_val = 0;
            }
            if (!po->name)
                po= find_option(options, "default");
            if (!po->name) {
unknown_opt:
                fprintf(stderr, "%s: unrecognized option '%s'\n", argv[0], opt);
                exit(1);
            }
            arg = NULL;
            if (po->flags & HAS_ARG) {
                arg = argv[optindex++];
                if (!arg) {
                    fprintf(stderr, "%s: missing argument for option '%s'\n", argv[0], opt);
                    exit(1);
                }
            }
            if (po->flags & OPT_STRING) {
                char *str;
                str = av_strdup(arg);
                *po->u.str_arg = str;
            } else if (po->flags & OPT_BOOL) {
                *po->u.int_arg = bool_val;
            } else if (po->flags & OPT_INT) {
                *po->u.int_arg = parse_number_or_die(opt, arg, OPT_INT64, INT_MIN, INT_MAX);
            } else if (po->flags & OPT_INT64) {
                *po->u.int64_arg = parse_number_or_die(opt, arg, OPT_INT64, INT64_MIN, INT64_MAX);
            } else if (po->flags & OPT_FLOAT) {
                *po->u.float_arg = parse_number_or_die(opt, arg, OPT_FLOAT, -INFINITY, INFINITY);
            } else if (po->u.func_arg) {
                if (po->u.func_arg(opt, arg) < 0) {
                    fprintf(stderr, "%s: failed to set value '%s' for option '%s'\n", argv[0], arg, opt);
                    exit(1);
                }
            }
            if(po->flags & OPT_EXIT)
                exit(0);
        } else {
            if (parse_arg_function) {
                if (parse_arg_function(NULL, opt) < 0)
                    exit(1);
            }
        }
    }
}

#define FLAGS (o->type == FF_OPT_TYPE_FLAGS) ? AV_DICT_APPEND : 0
#define SET_PREFIXED_OPTS(ch, flag, output) \
    if (opt[0] == ch && avcodec_opts[0] && (o = av_opt_find(avcodec_opts[0], opt+1, NULL, flag, 0)))\
        av_dict_set(&output, opt+1, arg, FLAGS);
static int opt_default2(const char *opt, const char *arg)
{
    const AVOption *o;
    if ((o = av_opt_find(avcodec_opts[0], opt, NULL, 0, AV_OPT_SEARCH_CHILDREN))) {
        if (o->flags & AV_OPT_FLAG_VIDEO_PARAM)
            av_dict_set(&video_opts, opt, arg, FLAGS);
        if (o->flags & AV_OPT_FLAG_AUDIO_PARAM)
            av_dict_set(&audio_opts, opt, arg, FLAGS);
        if (o->flags & AV_OPT_FLAG_SUBTITLE_PARAM)
            av_dict_set(&sub_opts, opt, arg, FLAGS);
    } else if ((o = av_opt_find(avformat_opts, opt, NULL, 0, AV_OPT_SEARCH_CHILDREN)))
        av_dict_set(&format_opts, opt, arg, FLAGS);
    else if ((o = av_opt_find(sws_opts, opt, NULL, 0, AV_OPT_SEARCH_CHILDREN))) {
        // XXX we only support sws_flags, not arbitrary sws options
        int ret = av_set_string3(sws_opts, opt, arg, 1, NULL);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error setting option %s.\n", opt);
            return ret;
        }
    }

    if (!o) {
        SET_PREFIXED_OPTS('v', AV_OPT_FLAG_VIDEO_PARAM,    video_opts)
        SET_PREFIXED_OPTS('a', AV_OPT_FLAG_AUDIO_PARAM,    audio_opts)
        SET_PREFIXED_OPTS('s', AV_OPT_FLAG_SUBTITLE_PARAM, sub_opts)
    }

    if (o)
        return 0;
    fprintf(stderr, "Unrecognized option '%s'\n", opt);
    return AVERROR_OPTION_NOT_FOUND;
}

int opt_default(const char *opt, const char *arg){
    int type;
    int ret= 0;
    const AVOption *o= NULL;
    int opt_types[]={AV_OPT_FLAG_VIDEO_PARAM, AV_OPT_FLAG_AUDIO_PARAM, 0, AV_OPT_FLAG_SUBTITLE_PARAM, 0};
    AVCodec *p = NULL;
    AVOutputFormat *oformat = NULL;
    AVInputFormat *iformat = NULL;

    while ((p = av_codec_next(p))) {
        const AVClass *c = p->priv_class;
        if (c && av_find_opt(&c, opt, NULL, 0, 0))
            break;
    }
    if (p)
        goto out;
    while ((oformat = av_oformat_next(oformat))) {
        const AVClass *c = oformat->priv_class;
        if (c && av_find_opt(&c, opt, NULL, 0, 0))
            break;
    }
    if (oformat)
        goto out;
    while ((iformat = av_iformat_next(iformat))) {
        const AVClass *c = iformat->priv_class;
        if (c && av_find_opt(&c, opt, NULL, 0, 0))
            break;
    }
    if (iformat)
        goto out;

    for(type=0; *avcodec_opts && type<AVMEDIA_TYPE_NB && ret>= 0; type++){
        const AVOption *o2 = av_opt_find(avcodec_opts[0], opt, NULL, opt_types[type], 0);
        if(o2)
            ret = av_set_string3(avcodec_opts[type], opt, arg, 1, &o);
    }
    if(!o && avformat_opts)
        ret = av_set_string3(avformat_opts, opt, arg, 1, &o);
    if(!o && sws_opts)
        ret = av_set_string3(sws_opts, opt, arg, 1, &o);
    if(!o){
        if (opt[0] == 'a' && avcodec_opts[AVMEDIA_TYPE_AUDIO])
            ret = av_set_string3(avcodec_opts[AVMEDIA_TYPE_AUDIO], opt+1, arg, 1, &o);
        else if(opt[0] == 'v' && avcodec_opts[AVMEDIA_TYPE_VIDEO])
            ret = av_set_string3(avcodec_opts[AVMEDIA_TYPE_VIDEO], opt+1, arg, 1, &o);
        else if(opt[0] == 's' && avcodec_opts[AVMEDIA_TYPE_SUBTITLE])
            ret = av_set_string3(avcodec_opts[AVMEDIA_TYPE_SUBTITLE], opt+1, arg, 1, &o);
        if (ret >= 0)
            opt += 1;
    }
    if (o && ret < 0) {
        fprintf(stderr, "Invalid value '%s' for option '%s'\n", arg, opt);
        exit(1);
    }
    if (!o) {
        fprintf(stderr, "Unrecognized option '%s'\n", opt);
        exit(1);
    }

 out:
    if ((ret = opt_default2(opt, arg)) < 0)
        return ret;

//    av_log(NULL, AV_LOG_ERROR, "%s:%s: %f 0x%0X\n", opt, arg, av_get_double(avcodec_opts, opt, NULL), (int)av_get_int(avcodec_opts, opt, NULL));

    opt_values= av_realloc(opt_values, sizeof(void*)*(opt_name_count+1));
    opt_values[opt_name_count] = av_strdup(arg);
    opt_names= av_realloc(opt_names, sizeof(void*)*(opt_name_count+1));
    opt_names[opt_name_count++] = av_strdup(opt);

    if ((*avcodec_opts && avcodec_opts[0]->debug) || (avformat_opts && avformat_opts->debug))
        av_log_set_level(AV_LOG_DEBUG);
    return 0;
}

int opt_loglevel(const char *opt, const char *arg)
{
    const struct { const char *name; int level; } log_levels[] = {
        { "quiet"  , AV_LOG_QUIET   },
        { "panic"  , AV_LOG_PANIC   },
        { "fatal"  , AV_LOG_FATAL   },
        { "error"  , AV_LOG_ERROR   },
        { "warning", AV_LOG_WARNING },
        { "info"   , AV_LOG_INFO    },
        { "verbose", AV_LOG_VERBOSE },
        { "debug"  , AV_LOG_DEBUG   },
    };
    char *tail;
    int level;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(log_levels); i++) {
        if (!strcmp(log_levels[i].name, arg)) {
            av_log_set_level(log_levels[i].level);
            return 0;
        }
    }

    level = strtol(arg, &tail, 10);
    if (*tail) {
        fprintf(stderr, "Invalid loglevel \"%s\". "
                        "Possible levels are numbers or:\n", arg);
        for (i = 0; i < FF_ARRAY_ELEMS(log_levels); i++)
            fprintf(stderr, "\"%s\"\n", log_levels[i].name);
        exit(1);
    }
    av_log_set_level(level);
    return 0;
}

int opt_timelimit(const char *opt, const char *arg)
{
#if HAVE_SETRLIMIT
    int lim = parse_number_or_die(opt, arg, OPT_INT64, 0, INT_MAX);
    struct rlimit rl = { lim, lim + 1 };
    if (setrlimit(RLIMIT_CPU, &rl))
        perror("setrlimit");
#else
    fprintf(stderr, "Warning: -%s not implemented on this OS\n", opt);
#endif
    return 0;
}

static void *alloc_priv_context(int size, const AVClass *class)
{
    void *p = av_mallocz(size);
    if (p) {
        *(const AVClass **)p = class;
        av_opt_set_defaults(p);
    }
    return p;
}

void set_context_opts(void *ctx, void *opts_ctx, int flags, AVCodec *codec)
{
    int i;
    void *priv_ctx=NULL;
    if(!strcmp("AVCodecContext", (*(AVClass**)ctx)->class_name)){
        AVCodecContext *avctx= ctx;
        if(codec && codec->priv_class){
            if(!avctx->priv_data && codec->priv_data_size)
                avctx->priv_data= alloc_priv_context(codec->priv_data_size, codec->priv_class);
            priv_ctx= avctx->priv_data;
        }
    } else if (!strcmp("AVFormatContext", (*(AVClass**)ctx)->class_name)) {
        AVFormatContext *avctx = ctx;
        if (avctx->oformat && avctx->oformat->priv_class) {
            priv_ctx = avctx->priv_data;
        } else if (avctx->iformat && avctx->iformat->priv_class) {
            priv_ctx = avctx->priv_data;
        }
    }

    for(i=0; i<opt_name_count; i++){
        char buf[256];
        const AVOption *opt;
        const char *str;
        if (priv_ctx) {
            if (av_find_opt(priv_ctx, opt_names[i], NULL, flags, flags)) {
                if (av_set_string3(priv_ctx, opt_names[i], opt_values[i], 1, NULL) < 0) {
                    fprintf(stderr, "Invalid value '%s' for option '%s'\n",
                            opt_names[i], opt_values[i]);
                    exit(1);
                }
            } else
                goto global;
        } else {
        global:
            str = av_get_string(opts_ctx, opt_names[i], &opt, buf, sizeof(buf));
            /* if an option with name opt_names[i] is present in opts_ctx then str is non-NULL */
            if (str && ((opt->flags & flags) == flags))
                av_set_string3(ctx, opt_names[i], str, 1, NULL);
        }
    }
}

void print_error(const char *filename, int err)
{
    char errbuf[128];
    const char *errbuf_ptr = errbuf;

    if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
        errbuf_ptr = strerror(AVUNERROR(err));
    fprintf(stderr, "%s: %s\n", filename, errbuf_ptr);
}

static int warned_cfg = 0;

#define INDENT        1
#define SHOW_VERSION  2
#define SHOW_CONFIG   4

#define PRINT_LIB_INFO(outstream,libname,LIBNAME,flags)                 \
    if (CONFIG_##LIBNAME) {                                             \
        const char *indent = flags & INDENT? "  " : "";                 \
        if (flags & SHOW_VERSION) {                                     \
            unsigned int version = libname##_version();                 \
            fprintf(outstream, "%slib%-9s %2d.%3d.%2d / %2d.%3d.%2d\n", \
                    indent, #libname,                                   \
                    LIB##LIBNAME##_VERSION_MAJOR,                       \
                    LIB##LIBNAME##_VERSION_MINOR,                       \
                    LIB##LIBNAME##_VERSION_MICRO,                       \
                    version >> 16, version >> 8 & 0xff, version & 0xff); \
        }                                                               \
        if (flags & SHOW_CONFIG) {                                      \
            const char *cfg = libname##_configuration();                \
            if (strcmp(FFMPEG_CONFIGURATION, cfg)) {                    \
                if (!warned_cfg) {                                      \
                    fprintf(outstream,                                  \
                            "%sWARNING: library configuration mismatch\n", \
                            indent);                                    \
                    warned_cfg = 1;                                     \
                }                                                       \
                fprintf(stderr, "%s%-11s configuration: %s\n",          \
                        indent, #libname, cfg);                         \
            }                                                           \
        }                                                               \
    }                                                                   \

static void print_all_libs_info(FILE* outstream, int flags)
{
    PRINT_LIB_INFO(outstream, avutil,   AVUTIL,   flags);
    PRINT_LIB_INFO(outstream, avcodec,  AVCODEC,  flags);
    PRINT_LIB_INFO(outstream, avformat, AVFORMAT, flags);
    PRINT_LIB_INFO(outstream, avdevice, AVDEVICE, flags);
    PRINT_LIB_INFO(outstream, avfilter, AVFILTER, flags);
    PRINT_LIB_INFO(outstream, swscale,  SWSCALE,  flags);
    PRINT_LIB_INFO(outstream, postproc, POSTPROC, flags);
}

void show_banner(void)
{
    fprintf(stderr, "%s version " FFMPEG_VERSION ", Copyright (c) %d-%d the FFmpeg developers\n",
            program_name, program_birth_year, this_year);
    fprintf(stderr, "  built on %s %s with %s %s\n",
            __DATE__, __TIME__, CC_TYPE, CC_VERSION);
    fprintf(stderr, "  configuration: " FFMPEG_CONFIGURATION "\n");
    print_all_libs_info(stderr, INDENT|SHOW_CONFIG);
    print_all_libs_info(stderr, INDENT|SHOW_VERSION);
}

void show_version(void) {
    printf("%s " FFMPEG_VERSION "\n", program_name);
    print_all_libs_info(stdout, SHOW_VERSION);
}

void show_license(void)
{
    printf(
#if CONFIG_NONFREE
    "This version of %s has nonfree parts compiled in.\n"
    "Therefore it is not legally redistributable.\n",
    program_name
#elif CONFIG_GPLV3
    "%s is free software; you can redistribute it and/or modify\n"
    "it under the terms of the GNU General Public License as published by\n"
    "the Free Software Foundation; either version 3 of the License, or\n"
    "(at your option) any later version.\n"
    "\n"
    "%s is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
    "GNU General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU General Public License\n"
    "along with %s.  If not, see <http://www.gnu.org/licenses/>.\n",
    program_name, program_name, program_name
#elif CONFIG_GPL
    "%s is free software; you can redistribute it and/or modify\n"
    "it under the terms of the GNU General Public License as published by\n"
    "the Free Software Foundation; either version 2 of the License, or\n"
    "(at your option) any later version.\n"
    "\n"
    "%s is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
    "GNU General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU General Public License\n"
    "along with %s; if not, write to the Free Software\n"
    "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA\n",
    program_name, program_name, program_name
#elif CONFIG_LGPLV3
    "%s is free software; you can redistribute it and/or modify\n"
    "it under the terms of the GNU Lesser General Public License as published by\n"
    "the Free Software Foundation; either version 3 of the License, or\n"
    "(at your option) any later version.\n"
    "\n"
    "%s is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
    "GNU Lesser General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU Lesser General Public License\n"
    "along with %s.  If not, see <http://www.gnu.org/licenses/>.\n",
    program_name, program_name, program_name
#else
    "%s is free software; you can redistribute it and/or\n"
    "modify it under the terms of the GNU Lesser General Public\n"
    "License as published by the Free Software Foundation; either\n"
    "version 2.1 of the License, or (at your option) any later version.\n"
    "\n"
    "%s is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU\n"
    "Lesser General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU Lesser General Public\n"
    "License along with %s; if not, write to the Free Software\n"
    "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA\n",
    program_name, program_name, program_name
#endif
    );
}

void show_formats(void)
{
    AVInputFormat *ifmt=NULL;
    AVOutputFormat *ofmt=NULL;
    const char *last_name;

    printf(
        "File formats:\n"
        " D. = Demuxing supported\n"
        " .E = Muxing supported\n"
        " --\n");
    last_name= "000";
    for(;;){
        int decode=0;
        int encode=0;
        const char *name=NULL;
        const char *long_name=NULL;

        while((ofmt= av_oformat_next(ofmt))) {
            if((name == NULL || strcmp(ofmt->name, name)<0) &&
                strcmp(ofmt->name, last_name)>0){
                name= ofmt->name;
                long_name= ofmt->long_name;
                encode=1;
            }
        }
        while((ifmt= av_iformat_next(ifmt))) {
            if((name == NULL || strcmp(ifmt->name, name)<0) &&
                strcmp(ifmt->name, last_name)>0){
                name= ifmt->name;
                long_name= ifmt->long_name;
                encode=0;
            }
            if(name && strcmp(ifmt->name, name)==0)
                decode=1;
        }
        if(name==NULL)
            break;
        last_name= name;

        printf(
            " %s%s %-15s %s\n",
            decode ? "D":" ",
            encode ? "E":" ",
            name,
            long_name ? long_name:" ");
    }
}

void show_codecs(void)
{
    AVCodec *p=NULL, *p2;
    const char *last_name;
    printf(
        "Codecs:\n"
        " D..... = Decoding supported\n"
        " .E.... = Encoding supported\n"
        " ..V... = Video codec\n"
        " ..A... = Audio codec\n"
        " ..S... = Subtitle codec\n"
        " ...S.. = Supports draw_horiz_band\n"
        " ....D. = Supports direct rendering method 1\n"
        " .....T = Supports weird frame truncation\n"
        " ------\n");
    last_name= "000";
    for(;;){
        int decode=0;
        int encode=0;
        int cap=0;
        const char *type_str;

        p2=NULL;
        while((p= av_codec_next(p))) {
            if((p2==NULL || strcmp(p->name, p2->name)<0) &&
                strcmp(p->name, last_name)>0){
                p2= p;
                decode= encode= cap=0;
            }
            if(p2 && strcmp(p->name, p2->name)==0){
                if(p->decode) decode=1;
                if(p->encode) encode=1;
                cap |= p->capabilities;
            }
        }
        if(p2==NULL)
            break;
        last_name= p2->name;

        switch(p2->type) {
        case AVMEDIA_TYPE_VIDEO:
            type_str = "V";
            break;
        case AVMEDIA_TYPE_AUDIO:
            type_str = "A";
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            type_str = "S";
            break;
        default:
            type_str = "?";
            break;
        }
        printf(
            " %s%s%s%s%s%s %-15s %s",
            decode ? "D": (/*p2->decoder ? "d":*/" "),
            encode ? "E":" ",
            type_str,
            cap & CODEC_CAP_DRAW_HORIZ_BAND ? "S":" ",
            cap & CODEC_CAP_DR1 ? "D":" ",
            cap & CODEC_CAP_TRUNCATED ? "T":" ",
            p2->name,
            p2->long_name ? p2->long_name : "");
       /* if(p2->decoder && decode==0)
            printf(" use %s for decoding", p2->decoder->name);*/
        printf("\n");
    }
    printf("\n");
    printf(
"Note, the names of encoders and decoders do not always match, so there are\n"
"several cases where the above table shows encoder only or decoder only entries\n"
"even though both encoding and decoding are supported. For example, the h263\n"
"decoder corresponds to the h263 and h263p encoders, for file formats it is even\n"
"worse.\n");
}

void show_bsfs(void)
{
    AVBitStreamFilter *bsf=NULL;

    printf("Bitstream filters:\n");
    while((bsf = av_bitstream_filter_next(bsf)))
        printf("%s\n", bsf->name);
    printf("\n");
}

void show_protocols(void)
{
    URLProtocol *up=NULL;

    printf("Supported file protocols:\n"
           "I.. = Input  supported\n"
           ".O. = Output supported\n"
           "..S = Seek   supported\n"
           "FLAGS NAME\n"
           "----- \n");
    while((up = av_protocol_next(up)))
        printf("%c%c%c   %s\n",
               up->url_read  ? 'I' : '.',
               up->url_write ? 'O' : '.',
               up->url_seek  ? 'S' : '.',
               up->name);
}

void show_filters(void)
{
    AVFilter av_unused(**filter) = NULL;

    printf("Filters:\n");
#if CONFIG_AVFILTER
    while ((filter = av_filter_next(filter)) && *filter)
        printf("%-16s %s\n", (*filter)->name, (*filter)->description);
#endif
}

void show_pix_fmts(void)
{
    enum PixelFormat pix_fmt;

    printf(
        "Pixel formats:\n"
        "I.... = Supported Input  format for conversion\n"
        ".O... = Supported Output format for conversion\n"
        "..H.. = Hardware accelerated format\n"
        "...P. = Paletted format\n"
        "....B = Bitstream format\n"
        "FLAGS NAME            NB_COMPONENTS BITS_PER_PIXEL\n"
        "-----\n");

#if !CONFIG_SWSCALE
#   define sws_isSupportedInput(x)  0
#   define sws_isSupportedOutput(x) 0
#endif

    for (pix_fmt = 0; pix_fmt < PIX_FMT_NB; pix_fmt++) {
        const AVPixFmtDescriptor *pix_desc = &av_pix_fmt_descriptors[pix_fmt];
        printf("%c%c%c%c%c %-16s       %d            %2d\n",
               sws_isSupportedInput (pix_fmt)      ? 'I' : '.',
               sws_isSupportedOutput(pix_fmt)      ? 'O' : '.',
               pix_desc->flags & PIX_FMT_HWACCEL   ? 'H' : '.',
               pix_desc->flags & PIX_FMT_PAL       ? 'P' : '.',
               pix_desc->flags & PIX_FMT_BITSTREAM ? 'B' : '.',
               pix_desc->name,
               pix_desc->nb_components,
               av_get_bits_per_pixel(pix_desc));
    }
}

int read_yesno(void)
{
    int c = getchar();
    int yesno = (toupper(c) == 'Y');

    while (c != '\n' && c != EOF)
        c = getchar();

    return yesno;
}

int read_file(const char *filename, char **bufptr, size_t *size)
{
    FILE *f = fopen(filename, "rb");

    if (!f) {
        fprintf(stderr, "Cannot read file '%s': %s\n", filename, strerror(errno));
        return AVERROR(errno);
    }
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    *bufptr = av_malloc(*size + 1);
    if (!*bufptr) {
        fprintf(stderr, "Could not allocate file buffer\n");
        fclose(f);
        return AVERROR(ENOMEM);
    }
    fread(*bufptr, 1, *size, f);
    (*bufptr)[*size++] = '\0';

    fclose(f);
    return 0;
}

FILE *get_preset_file(char *filename, size_t filename_size,
                      const char *preset_name, int is_path, const char *codec_name)
{
    FILE *f = NULL;
    int i;
    const char *base[3]= { getenv("FFMPEG_DATADIR"),
                           getenv("HOME"),
                           FFMPEG_DATADIR,
                         };

    if (is_path) {
        av_strlcpy(filename, preset_name, filename_size);
        f = fopen(filename, "r");
    } else {
#ifdef _WIN32
        char datadir[MAX_PATH], *ls;
        base[2] = NULL;

        if (GetModuleFileNameA(GetModuleHandleA(NULL), datadir, sizeof(datadir) - 1))
        {
            for (ls = datadir; ls < datadir + strlen(datadir); ls++)
                if (*ls == '\\') *ls = '/';

            if (ls = strrchr(datadir, '/'))
            {
                *ls = 0;
                strncat(datadir, "/ffpresets",  sizeof(datadir) - 1 - strlen(datadir));
                base[2] = datadir;
            }
        }
#endif
        for (i = 0; i < 3 && !f; i++) {
            if (!base[i])
                continue;
            snprintf(filename, filename_size, "%s%s/%s.ffpreset", base[i], i != 1 ? "" : "/.ffmpeg", preset_name);
            f = fopen(filename, "r");
            if (!f && codec_name) {
                snprintf(filename, filename_size,
                         "%s%s/%s-%s.ffpreset", base[i],  i != 1 ? "" : "/.ffmpeg", codec_name, preset_name);
                f = fopen(filename, "r");
            }
        }
    }

    return f;
}
