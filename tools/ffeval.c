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

#include <unistd.h>             /* getopt */
#include "libavutil/eval.h"

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

#define MAX_BLOCK_SIZE SIZE_MAX

int main(int argc, char **argv)
{
    size_t buf_size = 256;
    char *buf = av_malloc(buf_size);
    const char *outfilename = NULL, *infilename = NULL;
    FILE *outfile = NULL, *infile = NULL;
    const char *prompt = "=> ";
    int count = 0, echo = 0;
    char c;

    av_max_alloc(MAX_BLOCK_SIZE);

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

    if (!infilename || !strcmp(infilename, "-"))
        infilename = "/dev/stdin";
    infile = fopen(infilename, "r");
    if (!infile) {
        fprintf(stderr, "Impossible to open input file '%s': %s\n", infilename, strerror(errno));
        return 1;
    }

    if (!outfilename || !strcmp(outfilename, "-"))
        outfilename = "/dev/stdout";
    outfile = fopen(outfilename, "w");
    if (!outfile) {
        fprintf(stderr, "Impossible to open output file '%s': %s\n", outfilename, strerror(errno));
        return 1;
    }

    while ((c = fgetc(infile)) != EOF) {
        if (c == '\n') {
            double d;

            buf[count] = 0;
            if (buf[0] != '#') {
                av_expr_parse_and_eval(&d, buf,
                                       NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL, 0, NULL);
                if (echo)
                    fprintf(outfile, "%s ", buf);
                fprintf(outfile, "%s%f\n", prompt, d);
            }
            count = 0;
        } else {
            if (count >= buf_size-1) {
                if (buf_size == MAX_BLOCK_SIZE) {
                    av_log(NULL, AV_LOG_ERROR, "Memory allocation problem, "
                           "max block size '%zd' reached\n", MAX_BLOCK_SIZE);
                    return 1;
                }
                buf_size = FFMIN(buf_size, MAX_BLOCK_SIZE / 2) * 2;
                buf = av_realloc_f((void *)buf, buf_size, 1);
                if (!buf) {
                    av_log(NULL, AV_LOG_ERROR, "Memory allocation problem occurred\n");
                    return 1;
                }
            }
            buf[count++] = c;
        }
    }

    av_free(buf);
    return 0;
}
