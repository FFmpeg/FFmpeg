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

#include "config.h"
#if HAVE_UNISTD_H
#include <unistd.h>             /* getopt */
#endif

#include "libavutil/log.h"
#include "libavutil/bprint.h"

#if !HAVE_GETOPT
#include "compat/getopt.c"
#endif

/**
 * @file
 * escaping utility
 */

static void usage(void)
{
    printf("Escape an input string, adopting the av_get_token() escaping logic\n");
    printf("usage: ffescape [OPTIONS]\n");
    printf("\n"
           "Options:\n"
           "-e                echo each input line on output\n"
           "-h                print this help\n"
           "-i INFILE         set INFILE as input file, stdin if omitted\n"
           "-l LEVEL          set the number of escaping levels, 1 if omitted\n"
           "-m ESCAPE_MODE    select escape mode between 'full', 'lazy', 'quote', default is 'lazy'\n"
           "-o OUTFILE        set OUTFILE as output file, stdout if omitted\n"
           "-p PROMPT         set output prompt, is '=> ' by default\n"
           "-s SPECIAL_CHARS  set the list of special characters\n");
}

#define WHITESPACES " \n\t"

enum EscapeMode {
    ESCAPE_MODE_FULL,
    ESCAPE_MODE_LAZY,
    ESCAPE_MODE_QUOTE,
};

static int escape(char **dst, const char *src, const char *special_chars,
                  enum EscapeMode mode)
{
    AVBPrint dstbuf;

    av_bprint_init(&dstbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

    switch (mode) {
    case ESCAPE_MODE_FULL:
    case ESCAPE_MODE_LAZY:
        /* \-escape characters */

        if (mode == ESCAPE_MODE_LAZY && strchr(WHITESPACES, *src))
            av_bprintf(&dstbuf, "\\%c", *src++);

        for (; *src; src++) {
            if ((special_chars && strchr(special_chars, *src)) ||
                strchr("'\\", *src) ||
                (mode == ESCAPE_MODE_FULL && strchr(WHITESPACES, *src)))
                av_bprintf(&dstbuf, "\\%c", *src);
            else
                av_bprint_chars(&dstbuf, *src, 1);
        }

        if (mode == ESCAPE_MODE_LAZY && strchr(WHITESPACES, dstbuf.str[dstbuf.len-1])) {
            char c = dstbuf.str[dstbuf.len-1];
            dstbuf.str[dstbuf.len-1] = '\\';
            av_bprint_chars(&dstbuf, c, 1);
        }
        break;

    case ESCAPE_MODE_QUOTE:
        /* enclose between '' the string */
        av_bprint_chars(&dstbuf, '\'', 1);
        for (; *src; src++) {
            if (*src == '\'')
                av_bprintf(&dstbuf, "'\\''");
            else
                av_bprint_chars(&dstbuf, *src, 1);
        }
        av_bprint_chars(&dstbuf, '\'', 1);
        break;

    default:
        /* unknown escape mode */
        return AVERROR(EINVAL);
    }

    if (!av_bprint_is_complete(&dstbuf)) {
        av_bprint_finalize(&dstbuf, NULL);
        return AVERROR(ENOMEM);
    } else {
        av_bprint_finalize(&dstbuf, dst);
        return 0;
    }
}

int main(int argc, char **argv)
{
    AVBPrint src;
    char *src_buf, *dst_buf;
    const char *outfilename = NULL, *infilename = NULL;
    FILE *outfile = NULL, *infile = NULL;
    const char *prompt = "=> ";
    enum EscapeMode escape_mode = ESCAPE_MODE_LAZY;
    int level = 1;
    int echo = 0;
    char *special_chars = NULL;
    int c;

    while ((c = getopt(argc, argv, "ehi:l:o:m:p:s:")) != -1) {
        switch (c) {
        case 'e':
            echo = 1;
            break;
        case 'h':
            usage();
            return 0;
        case 'i':
            infilename = optarg;
            break;
        case 'l':
        {
            char *tail;
            long int li = strtol(optarg, &tail, 10);
            if (*tail || li > INT_MAX || li < 0) {
                av_log(NULL, AV_LOG_ERROR,
                        "Invalid value '%s' for option -l, argument must be a non negative integer\n",
                        optarg);
                return 1;
            }
            level = li;
            break;
        }
        case 'm':
            if      (!strcmp(optarg, "full"))  escape_mode = ESCAPE_MODE_FULL;
            else if (!strcmp(optarg, "lazy"))  escape_mode = ESCAPE_MODE_LAZY;
            else if (!strcmp(optarg, "quote")) escape_mode = ESCAPE_MODE_QUOTE;
            else {
                av_log(NULL, AV_LOG_ERROR,
                       "Invalid value '%s' for option -m, "
                       "valid arguments are 'full', 'lazy', 'quote'\n", optarg);
                return 1;
            }
            break;
        case 'o':
            outfilename = optarg;
            break;
        case 'p':
            prompt = optarg;
            break;
        case 's':
            special_chars = optarg;
            break;
        case '?':
            return 1;
        }
    }

    if (!infilename || !strcmp(infilename, "-")) {
        infilename = "stdin";
        infile = stdin;
    } else {
        infile = fopen(infilename, "r");
    }
    if (!infile) {
        av_log(NULL, AV_LOG_ERROR, "Impossible to open input file '%s': %s\n", infilename, strerror(errno));
        return 1;
    }

    if (!outfilename || !strcmp(outfilename, "-")) {
        outfilename = "stdout";
        outfile = stdout;
    } else {
        outfile = fopen(outfilename, "w");
    }
    if (!outfile) {
        av_log(NULL, AV_LOG_ERROR, "Impossible to open output file '%s': %s\n", outfilename, strerror(errno));
        return 1;
    }

    /* grab the input and store it in src */
    av_bprint_init(&src, 1, AV_BPRINT_SIZE_UNLIMITED);
    while ((c = fgetc(infile)) != EOF)
        av_bprint_chars(&src, c, 1);
    av_bprint_chars(&src, 0, 1);

    if (!av_bprint_is_complete(&src)) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate a buffer for the source string\n");
        av_bprint_finalize(&src, NULL);
        return 1;
    }
    av_bprint_finalize(&src, &src_buf);

    if (echo)
        fprintf(outfile, "%s", src_buf);

    /* escape */
    dst_buf = src_buf;
    while (level--) {
        if (escape(&dst_buf, src_buf, special_chars, escape_mode) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Could not escape string\n");
            return 1;
        }
        av_free(src_buf);
        src_buf = dst_buf;
    }

    fprintf(outfile, "%s%s", prompt, dst_buf);
    av_free(dst_buf);
    return 0;
}
