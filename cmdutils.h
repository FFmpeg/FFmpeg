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
extern AVCodecContext *avcodec_opts[AVMEDIA_TYPE_NB];
extern AVFormatContext *avformat_opts;
extern struct SwsContext *sws_opts;

/**
 * Trivial log callback.
 * Only suitable for show_help and similar since it lacks prefix handling.
 */
void log_callback_help(void* ptr, int level, const char* fmt, va_list vl);

/**
 * Fallback for options that are not explicitly handled, these will be
 * parsed through AVOptions.
 */
int opt_default(const char *opt, const char *arg);

/**
 * Set the libav* libraries log level.
 */
int opt_loglevel(const char *opt, const char *arg);

/**
 * Limit the execution time.
 */
int opt_timelimit(const char *opt, const char *arg);

/**
 * Parse a string and return its corresponding value as a double.
 * Exit from the application if the string cannot be correctly
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
 * Parse a string specifying a time and return its corresponding
 * value as a number of microseconds. Exit from the application if
 * the string cannot be correctly parsed.
 *
 * @param context the context of the value to be set (e.g. the
 * corresponding commandline option name)
 * @param timestr the string to be parsed
 * @param is_duration a flag which tells how to interpret timestr, if
 * not zero timestr is interpreted as a duration, otherwise as a
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
 * Parse the command line arguments.
 * @param options Array with the definitions required to interpret every
 * option of the form: -option_name [argument]
 * @param parse_arg_function Name of the function called to process every
 * argument without a leading option name flag. NULL if such arguments do
 * not have to be processed.
 */
void parse_options(int argc, char **argv, const OptionDef *options,
                   void (* parse_arg_function)(const char*));

void set_context_opts(void *ctx, void *opts_ctx, int flags);

/**
 * Print an error message to stderr, indicating filename and a human
 * readable description of the error code err.
 *
 * If strerror_r() is not available the use of this function in a
 * multithreaded application may be unsafe.
 *
 * @see av_strerror()
 */
void print_error(const char *filename, int err);

void list_fmts(void (*get_fmt_string)(char *buf, int buf_size, int fmt), int nb_fmts);

/**
 * Print the program banner to stderr. The banner contents depend on the
 * current version of the repository and of the libav* libraries used by
 * the program.
 */
void show_banner(void);

/**
 * Print the version of the program to stdout. The version message
 * depends on the current versions of the repository and of the libav*
 * libraries.
 */
void show_version(void);

/**
 * Print the license of the program to stdout. The license depends on
 * the license of the libraries compiled into the program.
 */
void show_license(void);

/**
 * Print a listing containing all the formats supported by the
 * program.
 */
void show_formats(void);

/**
 * Print a listing containing all the codecs supported by the
 * program.
 */
void show_codecs(void);

/**
 * Print a listing containing all the filters supported by the
 * program.
 */
void show_filters(void);

/**
 * Print a listing containing all the bit stream filters supported by the
 * program.
 */
void show_bsfs(void);

/**
 * Print a listing containing all the protocols supported by the
 * program.
 */
void show_protocols(void);

/**
 * Print a listing containing all the pixel formats supported by the
 * program.
 */
void show_pix_fmts(void);

/**
 * Return a positive value if a line read from standard input
 * starts with [yY], otherwise return 0.
 */
int read_yesno(void);

/**
 * Read the file with name filename, and put its content in a newly
 * allocated 0-terminated buffer.
 *
 * @param bufptr location where pointer to buffer is returned
 * @param size   location where size of buffer is returned
 * @return 0 in case of success, a negative value corresponding to an
 * AVERROR error code in case of failure.
 */
int read_file(const char *filename, char **bufptr, size_t *size);

typedef struct {
    int64_t num_faulty_pts; /// Number of incorrect PTS values so far
    int64_t num_faulty_dts; /// Number of incorrect DTS values so far
    int64_t last_pts;       /// PTS of the last frame
    int64_t last_dts;       /// DTS of the last frame
} PtsCorrectionContext;

/**
 * Resets the state of the PtsCorrectionContext.
 */
void init_pts_correction(PtsCorrectionContext *ctx);

/**
 * Attempts to guess proper monotonic timestamps for decoded video frames
 * which might have incorrect times. Input timestamps may wrap around, in
 * which case the output will as well.
 *
 * @param pts The pts field of the decoded AVPacket, as passed through
 * AVCodecContext.reordered_opaque
 * @param dts The dts field of the decoded AVPacket
 * @return One of the input values. May be AV_NOPTS_VALUE.
 */
int64_t guess_correct_pts(PtsCorrectionContext *ctx, int64_t pts, int64_t dts);

#endif /* FFMPEG_CMDUTILS_H */
