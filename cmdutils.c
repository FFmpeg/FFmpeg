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
#include "libavutil/mathematics.h"
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

struct SwsContext *sws_opts;
AVDictionary *format_opts, *codec_opts;

static const int this_year = 2011;

void init_opts(void)
{
#if CONFIG_SWSCALE
    sws_opts = sws_getContext(16, 16, 0, 16, 16, 0, SWS_BICUBIC, NULL, NULL, NULL);
#endif
}

void uninit_opts(void)
{
#if CONFIG_SWSCALE
    sws_freeContext(sws_opts);
    sws_opts = NULL;
#endif
    av_dict_free(&format_opts);
    av_dict_free(&codec_opts);
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
    av_log(NULL, AV_LOG_FATAL, error, context, numstr, min, max);
    exit_program(1);
    return 0;
}

int64_t parse_time_or_die(const char *context, const char *timestr, int is_duration)
{
    int64_t us;
    if (av_parse_time(&us, timestr, is_duration) < 0) {
        av_log(NULL, AV_LOG_FATAL, "Invalid %s specification for %s: %s\n",
               is_duration ? "duration" : "date", context, timestr);
        exit_program(1);
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

void show_help_children(const AVClass *class, int flags)
{
    const AVClass *child = NULL;
    av_opt_show2(&class, NULL, flags, 0);
    printf("\n");

    while (child = av_opt_child_class_next(class, child))
        show_help_children(child, flags);
}

static const OptionDef* find_option(const OptionDef *po, const char *name){
    const char *p = strchr(name, ':');
    int len = p ? p - name : strlen(name);

    while (po->name != NULL) {
        if (!strncmp(name, po->name, len) && strlen(po->name) == len)
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


int parse_option(void *optctx, const char *opt, const char *arg, const OptionDef *options)
{
    const OptionDef *po;
    int bool_val = 1;
    int *dstcount;
    void *dst;

    po = find_option(options, opt);
    if (!po->name && opt[0] == 'n' && opt[1] == 'o') {
        /* handle 'no' bool option */
        po = find_option(options, opt + 2);
        if (!(po->name && (po->flags & OPT_BOOL)))
            goto unknown_opt;
        bool_val = 0;
    }
    if (!po->name)
        po = find_option(options, "default");
    if (!po->name) {
unknown_opt:
        av_log(NULL, AV_LOG_ERROR, "Unrecognized option '%s'\n", opt);
        return AVERROR(EINVAL);
    }
    if (po->flags & HAS_ARG && !arg) {
        av_log(NULL, AV_LOG_ERROR, "Missing argument for option '%s'\n", opt);
        return AVERROR(EINVAL);
    }

    /* new-style options contain an offset into optctx, old-style address of
     * a global var*/
    dst = po->flags & (OPT_OFFSET|OPT_SPEC) ? (uint8_t*)optctx + po->u.off : po->u.dst_ptr;

    if (po->flags & OPT_SPEC) {
        SpecifierOpt **so = dst;
        char *p = strchr(opt, ':');

        dstcount = (int*)(so + 1);
        *so = grow_array(*so, sizeof(**so), dstcount, *dstcount + 1);
        (*so)[*dstcount - 1].specifier = av_strdup(p ? p + 1 : "");
        dst = &(*so)[*dstcount - 1].u;
    }

    if (po->flags & OPT_STRING) {
        char *str;
        str = av_strdup(arg);
        *(char**)dst = str;
    } else if (po->flags & OPT_BOOL) {
        *(int*)dst = bool_val;
    } else if (po->flags & OPT_INT) {
        *(int*)dst = parse_number_or_die(opt, arg, OPT_INT64, INT_MIN, INT_MAX);
    } else if (po->flags & OPT_INT64) {
        *(int64_t*)dst = parse_number_or_die(opt, arg, OPT_INT64, INT64_MIN, INT64_MAX);
    } else if (po->flags & OPT_TIME) {
        *(int64_t*)dst = parse_time_or_die(opt, arg, 1);
    } else if (po->flags & OPT_FLOAT) {
        *(float*)dst = parse_number_or_die(opt, arg, OPT_FLOAT, -INFINITY, INFINITY);
    } else if (po->flags & OPT_DOUBLE) {
        *(double*)dst = parse_number_or_die(opt, arg, OPT_DOUBLE, -INFINITY, INFINITY);
    } else if (po->u.func_arg) {
        int ret = po->flags & OPT_FUNC2 ? po->u.func2_arg(optctx, opt, arg) :
                                          po->u.func_arg(opt, arg);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Failed to set value '%s' for option '%s'\n", arg, opt);
            return ret;
        }
    }
    if (po->flags & OPT_EXIT)
        exit_program(0);
    return !!(po->flags & HAS_ARG);
}

void parse_options(void *optctx, int argc, char **argv, const OptionDef *options,
                   void (* parse_arg_function)(void *, const char*))
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
                exit_program(1);
            optindex += ret;
        } else {
            if (parse_arg_function)
                parse_arg_function(optctx, opt);
        }
    }
}

/*
 * Return index of option opt in argv or 0 if not found.
 */
static int locate_option(int argc, char **argv, const OptionDef *options, const char *optname)
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

        if (!po || po->flags & HAS_ARG)
            i++;
    }
    return 0;
}

void parse_loglevel(int argc, char **argv, const OptionDef *options)
{
    int idx = locate_option(argc, argv, options, "loglevel");
    if (!idx)
        idx = locate_option(argc, argv, options, "v");
    if (idx && argv[idx + 1])
        opt_loglevel("loglevel", argv[idx + 1]);
}

#define FLAGS(o) ((o)->type == AV_OPT_TYPE_FLAGS) ? AV_DICT_APPEND : 0
int opt_default(const char *opt, const char *arg)
{
    const AVOption *oc, *of, *os;
    char opt_stripped[128];
    const char *p;
    const AVClass *cc = avcodec_get_class(), *fc = avformat_get_class(), *sc;

    if (!(p = strchr(opt, ':')))
        p = opt + strlen(opt);
    av_strlcpy(opt_stripped, opt, FFMIN(sizeof(opt_stripped), p - opt + 1));

    if ((oc = av_opt_find(&cc, opt_stripped, NULL, 0, AV_OPT_SEARCH_CHILDREN|AV_OPT_SEARCH_FAKE_OBJ)) ||
         ((opt[0] == 'v' || opt[0] == 'a' || opt[0] == 's') &&
          (oc = av_opt_find(&cc, opt+1, NULL, 0, AV_OPT_SEARCH_FAKE_OBJ))))
        av_dict_set(&codec_opts, opt, arg, FLAGS(oc));
    if ((of = av_opt_find(&fc, opt, NULL, 0, AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ)))
        av_dict_set(&format_opts, opt, arg, FLAGS(of));
#if CONFIG_SWSCALE
    sc = sws_get_class();
    if ((os = av_opt_find(&sc, opt, NULL, 0, AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ))) {
        // XXX we only support sws_flags, not arbitrary sws options
        int ret = av_opt_set(sws_opts, opt, arg, 0);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error setting option %s.\n", opt);
            return ret;
        }
    }
#endif

    if (oc || of || os)
        return 0;
    av_log(NULL, AV_LOG_ERROR, "Unrecognized option '%s'\n", opt);
    return AVERROR_OPTION_NOT_FOUND;
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
        av_log(NULL, AV_LOG_FATAL, "Invalid loglevel \"%s\". "
               "Possible levels are numbers or:\n", arg);
        for (i = 0; i < FF_ARRAY_ELEMS(log_levels); i++)
            av_log(NULL, AV_LOG_FATAL, "\"%s\"\n", log_levels[i].name);
        exit_program(1);
    }
    av_log_set_level(level);
    return 0;
}

int opt_codec_debug(const char *opt, const char *arg)
{
    av_log_set_level(AV_LOG_DEBUG);
    return opt_default(opt, arg);
}

int opt_timelimit(const char *opt, const char *arg)
{
#if HAVE_SETRLIMIT
    int lim = parse_number_or_die(opt, arg, OPT_INT64, 0, INT_MAX);
    struct rlimit rl = { lim, lim + 1 };
    if (setrlimit(RLIMIT_CPU, &rl))
        perror("setrlimit");
#else
    av_log(NULL, AV_LOG_WARNING, "-%s not implemented on this OS\n", opt);
#endif
    return 0;
}

void print_error(const char *filename, int err)
{
    char errbuf[128];
    const char *errbuf_ptr = errbuf;

    if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
        errbuf_ptr = strerror(AVUNERROR(err));
    av_log(NULL, AV_LOG_ERROR, "%s: %s\n", filename, errbuf_ptr);
}

static int warned_cfg = 0;

#define INDENT        1
#define SHOW_VERSION  2
#define SHOW_CONFIG   4

#define PRINT_LIB_INFO(libname, LIBNAME, flags, level)                  \
    if (CONFIG_##LIBNAME) {                                             \
        const char *indent = flags & INDENT? "  " : "";                 \
        if (flags & SHOW_VERSION) {                                     \
            unsigned int version = libname##_version();                 \
            av_log(NULL, level, "%slib%-9s %2d.%3d.%2d / %2d.%3d.%2d\n",\
                   indent, #libname,                                    \
                   LIB##LIBNAME##_VERSION_MAJOR,                        \
                   LIB##LIBNAME##_VERSION_MINOR,                        \
                   LIB##LIBNAME##_VERSION_MICRO,                        \
                   version >> 16, version >> 8 & 0xff, version & 0xff); \
        }                                                               \
        if (flags & SHOW_CONFIG) {                                      \
            const char *cfg = libname##_configuration();                \
            if (strcmp(FFMPEG_CONFIGURATION, cfg)) {                    \
                if (!warned_cfg) {                                      \
                    av_log(NULL, level,                                 \
                            "%sWARNING: library configuration mismatch\n", \
                            indent);                                    \
                    warned_cfg = 1;                                     \
                }                                                       \
                av_log(NULL, level, "%s%-11s configuration: %s\n",      \
                        indent, #libname, cfg);                         \
            }                                                           \
        }                                                               \
    }                                                                   \

static void print_all_libs_info(int flags, int level)
{
    PRINT_LIB_INFO(avutil,   AVUTIL,   flags, level);
    PRINT_LIB_INFO(avcodec,  AVCODEC,  flags, level);
    PRINT_LIB_INFO(avformat, AVFORMAT, flags, level);
    PRINT_LIB_INFO(avdevice, AVDEVICE, flags, level);
    PRINT_LIB_INFO(avfilter, AVFILTER, flags, level);
    PRINT_LIB_INFO(swscale,  SWSCALE,  flags, level);
    PRINT_LIB_INFO(postproc, POSTPROC, flags, level);
}

void show_banner(void)
{
    av_log(NULL, AV_LOG_INFO, "%s version " FFMPEG_VERSION ", Copyright (c) %d-%d the FFmpeg developers\n",
           program_name, program_birth_year, this_year);
    av_log(NULL, AV_LOG_INFO, "  built on %s %s with %s %s\n",
           __DATE__, __TIME__, CC_TYPE, CC_VERSION);
    av_log(NULL, AV_LOG_INFO, "  configuration: " FFMPEG_CONFIGURATION "\n");
    print_all_libs_info(INDENT|SHOW_CONFIG,  AV_LOG_INFO);
    print_all_libs_info(INDENT|SHOW_VERSION, AV_LOG_INFO);
}

int opt_version(const char *opt, const char *arg) {
    av_log_set_callback(log_callback_help);
    printf("%s " FFMPEG_VERSION "\n", program_name);
    print_all_libs_info(SHOW_VERSION, AV_LOG_INFO);
    return 0;
}

int opt_license(const char *opt, const char *arg)
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
    return 0;
}

int opt_formats(const char *opt, const char *arg)
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
    return 0;
}

int opt_codecs(const char *opt, const char *arg)
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
    return 0;
}

int opt_bsfs(const char *opt, const char *arg)
{
    AVBitStreamFilter *bsf=NULL;

    printf("Bitstream filters:\n");
    while((bsf = av_bitstream_filter_next(bsf)))
        printf("%s\n", bsf->name);
    printf("\n");
    return 0;
}

int opt_protocols(const char *opt, const char *arg)
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
    return 0;
}

int opt_filters(const char *opt, const char *arg)
{
    AVFilter av_unused(**filter) = NULL;

    printf("Filters:\n");
#if CONFIG_AVFILTER
    while ((filter = av_filter_next(filter)) && *filter)
        printf("%-16s %s\n", (*filter)->name, (*filter)->description);
#endif
    return 0;
}

int opt_pix_fmts(const char *opt, const char *arg)
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
    return 0;
}

int show_sample_fmts(const char *opt, const char *arg)
{
    int i;
    char fmt_str[128];
    for (i = -1; i < AV_SAMPLE_FMT_NB; i++)
        printf("%s\n", av_get_sample_fmt_string(fmt_str, sizeof(fmt_str), i));
    return 0;
}

int read_yesno(void)
{
    int c = getchar();
    int yesno = (toupper(c) == 'Y');

    while (c != '\n' && c != EOF)
        c = getchar();

    return yesno;
}

int cmdutils_read_file(const char *filename, char **bufptr, size_t *size)
{
    int ret;
    FILE *f = fopen(filename, "rb");

    if (!f) {
        av_log(NULL, AV_LOG_ERROR, "Cannot read file '%s': %s\n", filename, strerror(errno));
        return AVERROR(errno);
    }
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    *bufptr = av_malloc(*size + 1);
    if (!*bufptr) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate file buffer\n");
        fclose(f);
        return AVERROR(ENOMEM);
    }
    ret = fread(*bufptr, 1, *size, f);
    if (ret < *size) {
        av_free(*bufptr);
        if (ferror(f)) {
            av_log(NULL, AV_LOG_ERROR, "Error while reading file '%s': %s\n",
                   filename, strerror(errno));
            ret = AVERROR(errno);
        } else
            ret = AVERROR_EOF;
    } else {
        ret = 0;
        (*bufptr)[*size++] = '\0';
    }

    fclose(f);
    return ret;
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

int check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec)
{
    if (*spec <= '9' && *spec >= '0')                                        /* opt:index */
        return strtol(spec, NULL, 0) == st->index;
    else if (*spec == 'v' || *spec == 'a' || *spec == 's' || *spec == 'd' || *spec == 't') { /* opt:[vasdt] */
        enum AVMediaType type;

        switch (*spec++) {
        case 'v': type = AVMEDIA_TYPE_VIDEO;    break;
        case 'a': type = AVMEDIA_TYPE_AUDIO;    break;
        case 's': type = AVMEDIA_TYPE_SUBTITLE; break;
        case 'd': type = AVMEDIA_TYPE_DATA;     break;
        case 't': type = AVMEDIA_TYPE_ATTACHMENT; break;
        default: abort(); // never reached, silence warning
        }
        if (type != st->codec->codec_type)
            return 0;
        if (*spec++ == ':') {                                   /* possibly followed by :index */
            int i, index = strtol(spec, NULL, 0);
            for (i = 0; i < s->nb_streams; i++)
                if (s->streams[i]->codec->codec_type == type && index-- == 0)
                   return i == st->index;
            return 0;
        }
        return 1;
    } else if (*spec == 'p' && *(spec + 1) == ':') {
        int prog_id, i, j;
        char *endptr;
        spec += 2;
        prog_id = strtol(spec, &endptr, 0);
        for (i = 0; i < s->nb_programs; i++) {
            if (s->programs[i]->id != prog_id)
                continue;

            if (*endptr++ == ':') {
                int stream_idx = strtol(endptr, NULL, 0);
                return (stream_idx >= 0 && stream_idx < s->programs[i]->nb_stream_indexes &&
                        st->index == s->programs[i]->stream_index[stream_idx]);
            }

            for (j = 0; j < s->programs[i]->nb_stream_indexes; j++)
                if (st->index == s->programs[i]->stream_index[j])
                    return 1;
        }
        return 0;
    } else if (!*spec) /* empty specifier, matches everything */
        return 1;

    av_log(s, AV_LOG_ERROR, "Invalid stream specifier: %s.\n", spec);
    return AVERROR(EINVAL);
}

AVDictionary *filter_codec_opts(AVDictionary *opts, enum CodecID codec_id, AVFormatContext *s, AVStream *st)
{
    AVDictionary    *ret = NULL;
    AVDictionaryEntry *t = NULL;
    AVCodec       *codec = s->oformat ? avcodec_find_encoder(codec_id) : avcodec_find_decoder(codec_id);
    int            flags = s->oformat ? AV_OPT_FLAG_ENCODING_PARAM : AV_OPT_FLAG_DECODING_PARAM;
    char          prefix = 0;
    const AVClass    *cc = avcodec_get_class();

    if (!codec)
        return NULL;

    switch (codec->type) {
    case AVMEDIA_TYPE_VIDEO:    prefix = 'v'; flags |= AV_OPT_FLAG_VIDEO_PARAM;    break;
    case AVMEDIA_TYPE_AUDIO:    prefix = 'a'; flags |= AV_OPT_FLAG_AUDIO_PARAM;    break;
    case AVMEDIA_TYPE_SUBTITLE: prefix = 's'; flags |= AV_OPT_FLAG_SUBTITLE_PARAM; break;
    }

    while (t = av_dict_get(opts, "", t, AV_DICT_IGNORE_SUFFIX)) {
        char *p = strchr(t->key, ':');

        /* check stream specification in opt name */
        if (p)
            switch (check_stream_specifier(s, st, p + 1)) {
            case  1: *p = 0; break;
            case  0:         continue;
            default:         return NULL;
            }

        if (av_opt_find(&cc, t->key, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ) ||
            (codec && codec->priv_class && av_opt_find(&codec->priv_class, t->key, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ)))
            av_dict_set(&ret, t->key, t->value, 0);
        else if (t->key[0] == prefix && av_opt_find(&cc, t->key+1, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ))
            av_dict_set(&ret, t->key+1, t->value, 0);

        if (p)
            *p = ':';
    }
    return ret;
}

AVDictionary **setup_find_stream_info_opts(AVFormatContext *s, AVDictionary *codec_opts)
{
    int i;
    AVDictionary **opts;

    if (!s->nb_streams)
        return NULL;
    opts = av_mallocz(s->nb_streams * sizeof(*opts));
    if (!opts) {
        av_log(NULL, AV_LOG_ERROR, "Could not alloc memory for stream options.\n");
        return NULL;
    }
    for (i = 0; i < s->nb_streams; i++)
        opts[i] = filter_codec_opts(codec_opts, s->streams[i]->codec->codec_id, s, s->streams[i]);
    return opts;
}

void *grow_array(void *array, int elem_size, int *size, int new_size)
{
    if (new_size >= INT_MAX / elem_size) {
        av_log(NULL, AV_LOG_ERROR, "Array too big.\n");
        exit_program(1);
    }
    if (*size < new_size) {
        uint8_t *tmp = av_realloc(array, new_size*elem_size);
        if (!tmp) {
            av_log(NULL, AV_LOG_ERROR, "Could not alloc buffer.\n");
            exit_program(1);
        }
        memset(tmp + *size*elem_size, 0, (new_size-*size) * elem_size);
        *size = new_size;
        return tmp;
    }
    return array;
}
