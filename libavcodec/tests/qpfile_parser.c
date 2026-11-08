/*
 * Copyright 2026 Lusoris
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
 * License for more details.
 */

#include <stdio.h>
#include <string.h>

#include "libavutil/error.h"
#include "libavutil/macros.h"
#include "libavutil/random_seed.h"
#include "libavcodec/qpfile_parser.h"

typedef struct FixtureCase {
    const char *name;
    const char *bytes;
    size_t len;
    void (*check)(const FFQpFile *qf);
} FixtureCase;

/* Return the short symbolic name of an error code. */
static const char *err_name(int err)
{
    if (!err)
        return "ok";
    if (err == AVERROR(EINVAL))
        return "EINVAL";
    if (err == AVERROR(ERANGE))
        return "ERANGE";
    if (err == AVERROR(ENOMEM))
        return "ENOMEM";
    return "error";
}

/* Print the frame index and block offsets for one frame. */
static void print_frame(const char *name, unsigned idx, const FFQpFrame *f)
{
    unsigned total = f->block_rows * f->block_cols;
    printf("%s frame %u: index %u offsets", name, idx, f->frame_index);
    for (unsigned i = 0; i < total; i++)
        printf(" %d", (int)f->block_offsets[i]);
    printf("\n");
}

/* Run additional validation assertions on the valid fixture. */
static void check_valid(const FFQpFile *qf)
{
    static const unsigned probe_indices[] = { 1, 2, 4, 5, 100 };
    int vals[8];
    unsigned n_vals;
    int err;

    print_frame("valid", 0, &qf->frames[0]);
    print_frame("valid", 1, &qf->frames[1]);

    err = ff_qpfile_check_grid(NULL, "test", qf, 32, 16);
    printf("valid check_grid 32x16: %s\n", err_name(err));
    err = ff_qpfile_check_grid(NULL, "test", qf, 48, 16);
    printf("valid check_grid 48x16: %s\n", err_name(err));

    for (unsigned i = 0; i < 5; i++) {
        const FFQpFrame *f = ff_qpfile_frame_for_index(qf, probe_indices[i]);
        if (f)
            printf("valid frame_for_index %u: %u\n", probe_indices[i], f->frame_index);
        else
            printf("valid frame_for_index %u: none\n", probe_indices[i]);
    }

    n_vals = ff_qpfile_segment_values(&qf->frames[1], vals, 8);
    printf("valid segment_values 1:");
    for (unsigned i = 0; i < n_vals; i++)
        printf(" %d", vals[i]);
    printf("\n");
}

/* Run additional validation assertions on the clip fixture. */
static void check_clip(const FFQpFile *qf)
{
    print_frame("clip", 0, &qf->frames[0]);
}

/* Run additional validation assertions on the header-only fixture. */
static void check_header_only(const FFQpFile *qf)
{
    int err = ff_qpfile_check_grid(NULL, "test", qf, 32, 16);
    printf("header-only check_grid 32x16: %s\n", err_name(err));
}

/* Print lookup results across populated, clearing, and resumed records. */
static void check_clearing(const FFQpFile *qf)
{
    static const unsigned probes[] = { 1, 2, 4, 5, 7, 8, 100 };

    for (unsigned i = 0; i < FF_ARRAY_ELEMS(probes); i++) {
        const FFQpFrame *f = ff_qpfile_frame_for_index(qf, probes[i]);

        if (!f)
            printf("clearing lookup %u: none\n", probes[i]);
        else if (!f->block_offsets)
            printf("clearing lookup %u: index %u clear\n",
                   probes[i], f->frame_index);
        else
            print_frame("clearing lookup", probes[i], f);
    }
}

/* Run additional validation assertions on the index-max fixture. */
static void check_index_max(const FFQpFile *qf)
{
    print_frame("index-max", 0, &qf->frames[0]);
}

/* Write bytes to a file path, returning 0 on success or -1 on error. */
static int write_fixture(const char *path, const char *bytes, size_t len)
{
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return -1;
    if (fwrite(bytes, 1, len, fp) != len) {
        fclose(fp);
        return -1;
    }
    if (fclose(fp) != 0)
        return -1;
    return 0;
}

/* Run one fixture test case against the temporary file path. */
static int run_fixture_test(const char *path, const FixtureCase *tc)
{
    FFQpFile qf = { 0 };
    int err, ret = 0;

    if (write_fixture(path, tc->bytes, tc->len) < 0) {
        printf("%s: cannot write fixture\n", tc->name);
        remove(path);
        return 1;
    }
    err = ff_qpfile_load(NULL, path, &qf);
    if (!err) {
        printf("%s: ok frames %u grid %ux%u\n", tc->name,
               qf.frame_count, qf.block_cols, qf.block_rows);
        if (tc->check)
            tc->check(&qf);
    } else {
        printf("%s: %s\n", tc->name, err_name(err));
    }
    ff_qpfile_free(&qf);
    if (remove(path) != 0) {
        printf("%s: cannot remove fixture\n", tc->name);
        ret = 1;
    }
    return ret;
}

/* Test and print one palette calculation result. */
static void test_palette(const char *label, const int8_t *vals, size_t n, unsigned budget)
{
    int out[32];
    unsigned count = ff_qpfile_palette(vals, n, out, budget);
    printf("%s:", label);
    if (!count) {
        printf(" (none)\n");
        return;
    }
    for (unsigned i = 0; i < count; i++)
        printf(" %d", out[i]);
    printf("\n");
}

/* Run the suite of palette calculation checks. */
static void test_palettes(void)
{
    static const int8_t p1[3] = { -12, 0, 12 };
    int8_t p_all[25];

    for (int i = 0; i < FF_ARRAY_ELEMS(p_all); i++)
        p_all[i] = (int8_t)(-12 + i);

    test_palette("palette -12 0 12 budget 8", p1, 3, 8);
    test_palette("palette -12 0 12 budget 2", p1, 3, 2);
    test_palette("palette -12..12 budget 8", p_all, 25, 8);
    test_palette("palette empty budget 8", NULL, 0, 8);
    test_palette("palette -12 0 12 budget 1", p1, 3, 1);
}

/* Test and print segment lookup-table indices for all offsets. */
static void test_lut(void)
{
    static const int seg[3] = { -12, 0, 12 };
    uint8_t lut[QPFILE_LUT_SIZE];
    ff_qpfile_segment_lut(lut, seg, 3);
    printf("lut -12 0 12:");
    for (unsigned i = 0; i < QPFILE_LUT_SIZE; i++)
        printf(" %u", (unsigned)lut[i]);
    printf("\n");
}

/* Main entry point for the QP file parser regression test. */
int main(void)
{
    static const FixtureCase cases[] = {
        { "valid", " \t\n# comment\n2 I 26\n-12 12\n  \t\n5 P 30\n3 -3\n",
          sizeof(" \t\n# comment\n2 I 26\n-12 12\n  \t\n5 P 30\n3 -3\n") - 1, check_valid },
        { "crlf", "2 I 26\r\n-12 12\r\n5 P 30\r\n3 -3\r\n",
          sizeof("2 I 26\r\n-12 12\r\n5 P 30\r\n3 -3\r\n") - 1, NULL },
        { "clip", "0 I 26\n-20 20\n",
          sizeof("0 I 26\n-20 20\n") - 1, check_clip },
        { "header-only", "0 I 26\n3 P 26\n",
          sizeof("0 I 26\n3 P 26\n") - 1, check_header_only },
        { "clearing", "2 I 26\n1 2\n5 P 26\n8 B 26\n-3 3\n",
          sizeof("2 I 26\n1 2\n5 P 26\n8 B 26\n-3 3\n") - 1, check_clearing },
        { "index-max", "4294967295 I 26\n-1 1\n",
          sizeof("4294967295 I 26\n-1 1\n") - 1, check_index_max },
        { "index-over", "4294967296 I 26\n",
          sizeof("4294967296 I 26\n") - 1, NULL },
        { "not-increasing", "5 I 26\n5 P 26\n",
          sizeof("5 I 26\n5 P 26\n") - 1, NULL },
        { "no-record", "# only a comment\n\n",
          sizeof("# only a comment\n\n") - 1, NULL },
        { "nul", "0 I 26\n-12 12\0junk\n",
          sizeof("0 I 26\n-12 12\0junk\n") - 1, NULL },
        { "row-width", "0 I 26\n1 2\n3\n",
          sizeof("0 I 26\n1 2\n3\n") - 1, NULL },
        { "frame-width", "0 I 26\n1 2\n1 P 26\n3\n",
          sizeof("0 I 26\n1 2\n1 P 26\n3\n") - 1, NULL },
        { "frame-rows", "0 I 26\n1 2\n1 P 26\n3 4\n5 6\n",
          sizeof("0 I 26\n1 2\n1 P 26\n3 4\n5 6\n") - 1, NULL },
    };
    char path[64];
    unsigned seed = av_get_random_seed();
    int ret = 0;

    snprintf(path, sizeof(path), "ff-qpfile-test-%08x.txt", seed);
    for (size_t i = 0; i < FF_ARRAY_ELEMS(cases); i++) {
        if (run_fixture_test(path, &cases[i]) != 0)
            ret = 1;
    }
    test_palettes();
    test_lut();
    return ret;
}
