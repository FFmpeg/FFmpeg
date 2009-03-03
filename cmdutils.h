/*
 * Various utilities for command line tools
 * copyright (c) 2003 Fabrice Bellard
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

#ifndef FFMPEG_CMDUTILS_H
#define FFMPEG_CMDUTILS_H

#include <inttypes.h>
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"

/**
 * program name, defined by the program for show_version().
 */
extern const char program_name[];

/**
 * program birth year, defined by the program for show_banner()
 */
extern const int program_birth_year;

extern const int this_year;

extern const char **opt_names;
extern AVCodecContext *avcodec_opts[CODEC_TYPE_NB];
extern AVFormatContext *avformat_opts;
extern struct SwsContext *sws_opts;

/**
 * Fallback for options that are not explicitly handled, these will be
 * parsed through AVOptions.
 */
int opt_default(const char *opt, const char *arg);

/**
 * Parses a string and returns its corresponding value as a double.
 * Exits from the application if the string cannot be correctly
 * parsed or the corresponding value is invalid.
 *
 * @param context the context of the value to be set (e.g. the
 * corresponding commandline option name)
 * @param numstr the string to be parsed
 * @param type the type (OPT_INT64 or OPT_FLOAT) as which the
 * string should be parsed
 * @param min the minimum valid accepted value
 * @param max the maximum valid accepted value
 */
double parse_number_or_die(const char *context, const char *numstr, int type, double min, double max);

/**
 * Parses a string specifying a time and returns its corresponding
 * value as a number of microseconds. Exits from the application if
 * the string cannot be correctly parsed.
 *
 * @param context the context of the value to be set (e.g. the
 * corresponding commandline option name)
 * @param timestr the string to be parsed
 * @param is_duration a flag which tells how to interpret \p timestr, if
 * not zero \p timestr is interpreted as a duration, otherwise as a
 * date
 *
 * @see parse_date()
 */
int64_t parse_time_or_die(const char *context, const char *timestr, int is_duration);

typedef struct {
    const char *name;
    int flags;
#define HAS_ARG    0x0001
#define OPT_BOOL   0x0002
#define OPT_EXPERT 0x0004
#define OPT_STRING 0x0008
#define OPT_VIDEO  0x0010
#define OPT_AUDIO  0x0020
#define OPT_GRAB   0x0040
#define OPT_INT    0x0080
#define OPT_FLOAT  0x0100
#define OPT_SUBTITLE 0x0200
#define OPT_FUNC2  0x0400
#define OPT_INT64  0x0800
#define OPT_EXIT   0x1000
     union {
        void (*func_arg)(const char *); //FIXME passing error code as int return would be nicer then exit() in the func
        int *int_arg;
        char **str_arg;
        float *float_arg;
        int (*func2_arg)(const char *, const char *);
        int64_t *int64_arg;
    } u;
    const char *help;
    const char *argname;
} OptionDef;

void show_help_options(const OptionDef *options, const char *msg, int mask, int value);

/**
 * Parses the command line arguments.
 * @param options Array with the definitions required to interpret every
 * option of the form: -<option_name> [<argument>]
 * @param parse_arg_function Name of the function called to process every
 * argument without a leading option name flag. NULL if such arguments do
 * not have to be processed.
 */
void parse_options(int argc, char **argv, const OptionDef *options,
                   void (* parse_arg_function)(const char*));

void set_context_opts(void *ctx, void *opts_ctx, int flags);

void print_error(const char *filename, int err);

/**
 * Prints the program banner to stderr. The banner contents depend on the
 * current version of the repository and of the libav* libraries used by
 * the program.
 */
void show_banner(void);

/**
 * Prints the version of the program to stdout. The version message
 * depends on the current versions of the repository and of the libav*
 * libraries.
 */
void show_version(void);

/**
 * Prints the license of the program to stdout. The license depends on
 * the license of the libraries compiled into the program.
 */
void show_license(void);

/**
 * Prints a listing containing all the formats supported by the
 * program.
 */
void show_formats(void);

#endif /* FFMPEG_CMDUTILS_H */
