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

#include "libavutil/eval.h"
#include "libavutil/mem.h"

#if !HAVE_GETOPT
#include "compat/getopt.c"
#endif

/**
 * @file
 * simple arithmetic expression evaluator
 */

static void usage(void)
{
    printf("Simple expression evalutor, please *don't* turn me to a feature-complete language interpreter\n");
    printf("usage: ffeval [OPTIONS]\n");
    printf("\n"
           "Options:\n"
           "-e                echo each input line on output\n"
           "-h                print this help\n"
           "-i INFILE         set INFILE as input file, stdin if omitted\n"
           "-o OUTFILE        set OUTFILE as output file, stdout if omitted\n"
           "-p PROMPT         set output prompt\n");
}

int main(int argc, char **argv)
{
    int buf_size = 0;
    char *buf = NULL;
    const char *outfilename = NULL, *infilename = NULL;
    FILE *outfile = NULL, *infile = NULL;
    const char *prompt = "=> ";
    int count = 0, echo = 0;
    int c;

#define GROW_ARRAY()                                                    \
    do {                                                                \
        if (!av_dynarray2_add((void **)&buf, &buf_size, 1, NULL)) {     \
            av_log(NULL, AV_LOG_ERROR,                                  \
                   "Memory allocation problem occurred\n");             \
            return 1;                                                   \
        }                                                               \
    } while (0)

    GROW_ARRAY();
    while ((c = getopt(argc, argv, "ehi:o:p:")) != -1) {
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
        case 'o':
            outfilename = optarg;
            break;
        case 'p':
            prompt = optarg;
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
        fprintf(stderr, "Impossible to open input file '%s': %s\n", infilename, strerror(errno));
        return 1;
    }

    if (!outfilename || !strcmp(outfilename, "-")) {
        outfilename = "stdout";
        outfile = stdout;
    } else {
        outfile = fopen(outfilename, "w");
    }
    if (!outfile) {
        fprintf(stderr, "Impossible to open output file '%s': %s\n", outfilename, strerror(errno));
        return 1;
    }

    while ((c = fgetc(infile)) != EOF) {
        if (c == '\n') {
            double d;

            buf[count] = 0;
            if (buf[0] != '#') {
                int ret = av_expr_parse_and_eval(&d, buf,
                                                 NULL, NULL,
                                                 NULL, NULL, NULL, NULL, NULL, 0, NULL);
                if (echo)
                    fprintf(outfile, "%s ", buf);
                if (ret >= 0) fprintf(outfile, "%s%f\n", prompt, d);
                else          fprintf(outfile, "%s%f (%s)\n", prompt, d, av_err2str(ret));
            }
            count = 0;
        } else {
            if (count >= buf_size-1)
                GROW_ARRAY();
            buf[count++] = c;
        }
    }

    av_free(buf);
    return 0;
}
