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

#include "avformat.h"
#include "cmdutils.h"
#include "avstring.h"
#include "version.h"
#include "config.h"

#undef exit

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

void parse_options(int argc, char **argv, const OptionDef *options,
                   void (* parse_arg_function)(const char*))
{
    const char *opt, *arg;
    int optindex, handleoptions=1;
    const OptionDef *po;

    /* parse options */
    optindex = 1;
    while (optindex < argc) {
        opt = argv[optindex++];

        if (handleoptions && opt[0] == '-' && opt[1] != '\0') {
          if (opt[1] == '-' && opt[2] == '\0') {
            handleoptions = 0;
            continue;
          }
            po= find_option(options, opt + 1);
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
                *po->u.int_arg = 1;
            } else if (po->flags & OPT_INT) {
                *po->u.int_arg = atoi(arg);
            } else if (po->flags & OPT_INT64) {
                *po->u.int64_arg = strtoll(arg, (char **)NULL, 10);
            } else if (po->flags & OPT_FLOAT) {
                *po->u.float_arg = atof(arg);
            } else if (po->flags & OPT_FUNC2) {
                if(po->u.func2_arg(opt+1, arg)<0)
                    goto unknown_opt;
            } else {
                po->u.func_arg(arg);
            }
        } else {
            if (parse_arg_function)
                parse_arg_function(opt);
        }
    }
}

void print_error(const char *filename, int err)
{
    switch(err) {
    case AVERROR_NUMEXPECTED:
        fprintf(stderr, "%s: Incorrect image filename syntax.\n"
                "Use '%%d' to specify the image number:\n"
                "  for img1.jpg, img2.jpg, ..., use 'img%%d.jpg';\n"
                "  for img001.jpg, img002.jpg, ..., use 'img%%03d.jpg'.\n",
                filename);
        break;
    case AVERROR_INVALIDDATA:
        fprintf(stderr, "%s: Error while parsing header\n", filename);
        break;
    case AVERROR_NOFMT:
        fprintf(stderr, "%s: Unknown format\n", filename);
        break;
    case AVERROR(EIO):
        fprintf(stderr, "%s: I/O error occured\n"
                "Usually that means that input file is truncated and/or corrupted.\n",
                filename);
        break;
    case AVERROR(ENOMEM):
        fprintf(stderr, "%s: memory allocation error occured\n", filename);
        break;
    case AVERROR(ENOENT):
        fprintf(stderr, "%s: no such file or directory\n", filename);
        break;
    default:
        fprintf(stderr, "%s: Error while opening file\n", filename);
        break;
    }
}

void show_banner(const char *program_name, int program_birth_year)
{
    fprintf(stderr, "%s version " FFMPEG_VERSION ", Copyright (c) %d-2007 Fabrice Bellard, et al.\n",
            program_name, program_birth_year);
    fprintf(stderr, "  configuration: " FFMPEG_CONFIGURATION "\n");
    fprintf(stderr, "  libavutil version: " AV_STRINGIFY(LIBAVUTIL_VERSION) "\n");
    fprintf(stderr, "  libavcodec version: " AV_STRINGIFY(LIBAVCODEC_VERSION) "\n");
    fprintf(stderr, "  libavformat version: " AV_STRINGIFY(LIBAVFORMAT_VERSION) "\n");
    fprintf(stderr, "  built on " __DATE__ " " __TIME__);
#ifdef __GNUC__
    fprintf(stderr, ", gcc: " __VERSION__ "\n");
#else
    fprintf(stderr, ", using a non-gcc compiler\n");
#endif
}

void show_version(const char *program_name) {
     /* TODO: add function interface to avutil and avformat */
    printf("%s " FFMPEG_VERSION "\n", program_name);
    printf("libavutil   %d\n"
           "libavcodec  %d\n"
           "libavformat %d\n",
           LIBAVUTIL_BUILD, avcodec_build(), LIBAVFORMAT_BUILD);
}

void show_license(void)
{
#ifdef CONFIG_GPL
    printf(
    "FFmpeg is free software; you can redistribute it and/or modify\n"
    "it under the terms of the GNU General Public License as published by\n"
    "the Free Software Foundation; either version 2 of the License, or\n"
    "(at your option) any later version.\n"
    "\n"
    "FFmpeg is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
    "GNU General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU General Public License\n"
    "along with FFmpeg; if not, write to the Free Software\n"
    "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA\n"
    );
#else
    printf(
    "FFmpeg is free software; you can redistribute it and/or\n"
    "modify it under the terms of the GNU Lesser General Public\n"
    "License as published by the Free Software Foundation; either\n"
    "version 2.1 of the License, or (at your option) any later version.\n"
    "\n"
    "FFmpeg is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU\n"
    "Lesser General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU Lesser General Public\n"
    "License along with FFmpeg; if not, write to the Free Software\n"
    "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA\n"
    );
#endif
}
