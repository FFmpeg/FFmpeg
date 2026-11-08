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

/**
 * @file
 * Shared parser for the per-frame QP-offset file format consumed by libx264,
 * libsvtav1, and libaom-av1 encoder adapters. The format consists of
 * <frame_idx> <I|P|B> <qp> followed by per-MB-row deltas.
 *
 * Format (ASCII, one logical record per frame):
 *
 *     <frame_idx> <I|P|B> <baseline_qp>
 *     <delta_0_0> <delta_0_1> ... <delta_0_(bw-1)>
 *     ...
 *
 * Lines are read with fgetc(), not POSIX getline().
 */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/common.h"
#include "libavutil/avstring.h"
#include "libavutil/error.h"
#include "libavutil/file_open.h"
#include "libavutil/log.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"

#include "qpfile_parser.h"

#define QPFILE_MAX_FRAMES         (1u << 20)
#define QPFILE_MAX_BLOCKS_PER_DIM (1u << 14)
#define QPFILE_MAX_LINE_BYTES     (1u << 20)

/* Parse a base-10 64-bit integer from a string, returning 0 or an error code. */
static int parse_int64(const char *s, char **end, int64_t *out)
{
    int64_t v;
    errno = 0;
    v = strtoll(s, end, 10);
    if (errno == ERANGE)
        return AVERROR(ERANGE);
    if (*end == s)
        return AVERROR(EINVAL);
    *out = v;
    return 0;
}

/* Clamp a QP delta value to the allowed range, returning the clamped value. */
static int clamp_offset(int64_t v)
{
    return av_clip64(v, QPFILE_DELTA_MIN, QPFILE_DELTA_MAX);
}

/* Skip spaces and tabs, returning the first other character. */
static const char *skip_blank(const char *p)
{
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

/* Test whether c is a frame type letter (I, P or B, any case), returning 1 or 0. */
static int is_frame_type(char c)
{
    return c && strchr("IPBipb", c);
}

/* Test whether a line matches the frame header format, returning 1 or 0. */
static int looks_like_header(const char *line)
{
    const char *p = skip_blank(line);

    if (!av_isdigit(*p))
        return 0;
    while (av_isdigit(*p))
        p++;
    return is_frame_type(*skip_blank(p));
}

/* Return 1 if p contains only whitespace (' ', '\t', '\r', '\n') up to '\0', else 0. */
static int only_space(const char *p)
{
    while (*p) {
        if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
            return 0;
        p++;
    }
    return 1;
}

/* Parse a frame header line into an FFQpFrame, returning 0 or an error code. */
static int parse_header(const char *line, FFQpFrame *f)
{
    char *p = NULL;
    const char *cur;
    int64_t idx = 0, qp = 0;
    int err;

    err = parse_int64(line, &p, &idx);
    if (err < 0)
        return err;
    if (idx < 0 || idx > UINT_MAX)
        return AVERROR(EINVAL);
    cur = skip_blank(p);
    if (!is_frame_type(*cur))
        return AVERROR(EINVAL);
    err = parse_int64(skip_blank(cur + 1), &p, &qp);
    if (err < 0)
        return err;
    if (qp < 0 || qp > 63 || !only_space(p))
        return AVERROR(EINVAL);

    f->frame_index   = (unsigned)idx;
    f->block_offsets = NULL;
    f->block_cols    = 0;
    f->block_rows    = 0;
    return 0;
}

/* Count whitespace-separated tokens on a line, returning the token count. */
static unsigned count_tokens(const char *line)
{
    unsigned n = 0;
    int in_tok = 0;
    for (const char *p = line; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            in_tok = 0;
        } else if (!in_tok) {
            in_tok = 1;
            n++;
            if (n > QPFILE_MAX_BLOCKS_PER_DIM)
                return n;
        }
    }
    return n;
}

/* Parse a row of clamped block QP deltas, returning 0 or a negative error. */
static int parse_row(const char *line, int8_t *row_out, unsigned bw)
{
    char *p = NULL;
    const char *cur = line;
    int64_t v = 0;
    for (unsigned i = 0; i < bw; i++) {
        int err;
        err = parse_int64(cur, &p, &v);
        if (err < 0)
            return err;
        row_out[i] = (int8_t)clamp_offset(v);
        cur = p;
        while (*cur == ' ' || *cur == '\t')
            cur++;
    }
    if (!only_space(cur))
        return AVERROR(EINVAL);
    return 0;
}

/* Allocate the first line buffer, returning 0 or an error code. */
static int qpfile_line_alloc(char **buf, size_t *cap)
{
    if (*cap && *buf)
        return 0;
    *cap = 4096;
    *buf = av_realloc(*buf, *cap);
    return *buf ? 0 : AVERROR(ENOMEM);
}

/* Grow the line buffer to fit one more byte and the terminator, returning 0 or an error code. */
static int qpfile_line_grow(char **buf, size_t *cap, size_t pos)
{
    size_t nc;
    char *nb;

    if (pos + 1 < *cap)
        return 0;
    nc = FFMIN((*cap) * 2u, (size_t)QPFILE_MAX_LINE_BYTES + 1);
    nb = av_realloc(*buf, nc);
    if (!nb)
        return AVERROR(ENOMEM);
    *buf = nb;
    *cap = nc;
    return 0;
}

/* End the line at end of file, setting out_len to -1 when nothing was read. */
static int qpfile_line_eof(FILE *fp, char *buf, size_t pos, ptrdiff_t *out_len)
{
    if (ferror(fp))
        return AVERROR(EIO);
    if (!pos) {
        *out_len = -1;
        return 0;
    }
    buf[pos] = 0;
    *out_len = (ptrdiff_t)pos;
    return 0;
}

/* Portable line reader (no POSIX getline dependency). */
static int qpfile_getline(char **buf, size_t *cap, FILE *fp, ptrdiff_t *out_len)
{
    size_t pos = 0;
    int ret = qpfile_line_alloc(buf, cap);

    if (ret < 0)
        return ret;
    while (1) {
        int c;

        ret = qpfile_line_grow(buf, cap, pos);
        if (ret < 0)
            return ret;
        c = fgetc(fp);
        if (c == EOF)
            return qpfile_line_eof(fp, *buf, pos, out_len);
        if (!c || pos >= QPFILE_MAX_LINE_BYTES)
            return AVERROR(EINVAL);
        (*buf)[pos++] = (char)c;
        if (c == '\n') {
            (*buf)[pos] = 0;
            *out_len = (ptrdiff_t)pos;
            return 0;
        }
    }
}

typedef struct QpLoadState {
    void *log_ctx;
    unsigned bw, bh, cap_frames, row_count, line_number;
    int8_t *row_buf;
    FFQpFrame *cur_frame;
} QpLoadState;

/* Finalise the current frame's block buffer, returning 0 or an error code. */
static int qpfile_finish_frame(QpLoadState *st)
{
    FFQpFrame *f = st->cur_frame;
    if (f && st->row_count > 0) {
        if (!st->bw) {
            st->bw = f->block_cols;
            st->bh = st->row_count;
        } else if (st->row_count != st->bh) {
            return AVERROR(EINVAL);
        }
        f->block_offsets = av_memdup(st->row_buf, (size_t)st->bh * st->bw * sizeof(*st->row_buf));
        if (!f->block_offsets)
            return AVERROR(ENOMEM);
        f->block_rows = st->bh;
        f->block_cols = st->bw;
    }
    return 0;
}

/* Append a new frame from a header line, returning 0 or a negative error. */
static int qpfile_add_header(FFQpFile *out, QpLoadState *st, const char *line)
{
    int ret = qpfile_finish_frame(st);

    if (ret < 0)
        return ret;
    if (out->frame_count == st->cap_frames) {
        unsigned nc = st->cap_frames * 2u;
        FFQpFrame *nf;
        if (nc > QPFILE_MAX_FRAMES)
            return AVERROR(EINVAL);
        nf = av_realloc_array(out->frames, nc, sizeof(*nf));
        if (!nf)
            return AVERROR(ENOMEM);
        memset(nf + st->cap_frames, 0, (nc - st->cap_frames) * sizeof(*nf));
        out->frames = nf;
        st->cap_frames = nc;
    }
    st->cur_frame = &out->frames[out->frame_count];
    ret = parse_header(line, st->cur_frame);
    if (ret < 0)
        return ret;
    if (out->frame_count > 0) {
        FFQpFrame *previous;
        previous = &out->frames[out->frame_count - 1u];
        if (st->cur_frame->frame_index <= previous->frame_index) {
            av_log(st->log_ctx, AV_LOG_ERROR,
                   "qpfile: line %u frame index %u is not greater than "
                   "previous index %u\n", st->line_number,
                   st->cur_frame->frame_index, previous->frame_index);
            return AVERROR(EINVAL);
        }
    }
    out->frame_count++;
    st->row_count = 0;
    return 0;
}

/* Set or check the frame's row width, returning 0 or an error code. */
static int qpfile_set_row_width(QpLoadState *st, unsigned tokens)
{
    FFQpFrame *f = st->cur_frame;

    if (tokens > QPFILE_MAX_BLOCKS_PER_DIM)
        return AVERROR(EINVAL);
    if (!f->block_cols) {
        f->block_cols = tokens;
        if (st->bw && tokens != st->bw)
            return AVERROR(EINVAL);
    } else if (tokens != f->block_cols) {
        return AVERROR(EINVAL);
    }
    return 0;
}

/* Parse and append a block delta row to the frame, returning 0 or an error. */
static int qpfile_add_row(QpLoadState *st, const char *line)
{
    unsigned tokens, this_bw, needed;
    int8_t *nrow;
    int ret;
    if (!st->cur_frame)
        return AVERROR(EINVAL);
    tokens = count_tokens(line);
    if (!tokens)
        return 0;
    ret = qpfile_set_row_width(st, tokens);
    if (ret < 0)
        return ret;
    this_bw = st->cur_frame->block_cols;
    needed = (st->row_count + 1) * this_bw;
    nrow = av_realloc_array(st->row_buf, needed, sizeof(*st->row_buf));
    if (!nrow)
        return AVERROR(ENOMEM);
    st->row_buf = nrow;
    ret = parse_row(line, &st->row_buf[st->row_count * this_bw], this_bw);
    if (ret < 0)
        return ret;
    st->row_count++;
    if (st->row_count > QPFILE_MAX_BLOCKS_PER_DIM)
        return AVERROR(EINVAL);
    return 0;
}

/* Log a line-level parser failure and return the error code. */
static int qpfile_line_error(const QpLoadState *st, const char *fmt_kind,
                             unsigned line, int err)
{
    av_log(st->log_ctx, AV_LOG_ERROR,
           "qpfile: %s line %u: %s\n", fmt_kind, line, av_err2str(err));
    return err;
}

/* Parse every frame header and row line in a file, returning 0 or an error. */
static int qpfile_read_frames(FILE *fp, FFQpFile *out, QpLoadState *st,
                              char **line, size_t *line_cap)
{
    ptrdiff_t got = 0;
    int ret;

    for (;;) {
        ret = qpfile_getline(line, line_cap, fp, &got);
        if (ret < 0)
            return qpfile_line_error(st, "error reading", st->line_number + 1, ret);
        if (got < 0)
            break;
        st->line_number++;
        if (only_space(*line) || (*line)[0] == '#')
            continue;
        if (looks_like_header(*line))
            ret = qpfile_add_header(out, st, *line);
        else
            ret = qpfile_add_row(st, *line);
        if (ret < 0)
            return qpfile_line_error(st, "invalid", st->line_number, ret);
    }
    ret = qpfile_finish_frame(st);
    if (ret < 0)
        return qpfile_line_error(st, "invalid last record ending at",
                                 st->line_number, ret);
    return 0;
}

/* Load and parse a QP-offset file from disk into out. */
int ff_qpfile_load(void *log_ctx, const char *path, FFQpFile *out)
{
    FILE *fp;
    char *line = NULL;
    size_t line_cap = 0;
    QpLoadState st = { 0 };
    int ret;

    if (!path || !out)
        return AVERROR(EINVAL);
    memset(out, 0, sizeof(*out));
    st.log_ctx = log_ctx;
    fp = avpriv_fopen_utf8(path, "rb");
    if (!fp)
        return AVERROR(errno ? errno : ENOENT);
    st.cap_frames = 16;
    out->frames = av_calloc(st.cap_frames, sizeof(*out->frames));
    if (!out->frames)
        ret = AVERROR(ENOMEM);
    else
        ret = qpfile_read_frames(fp, out, &st, &line, &line_cap);
    av_freep(&st.row_buf);
    if (ret >= 0) {
        out->block_cols = st.bw;
        out->block_rows = st.bh;
        if (!out->frame_count) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "qpfile: %s holds no frame record.\n", path);
            ret = AVERROR(EINVAL);
        }
    }
    av_free(line);
    fclose(fp);
    if (ret < 0)
        ff_qpfile_free(out);
    return ret;
}

/* Verify that the QP file grid matches the video macroblock dimensions. */
int ff_qpfile_check_grid(void *log_ctx, const char *encoder, const FFQpFile *qf,
                         int width, int height)
{
    const unsigned mbx = (width  + 15) / 16;
    const unsigned mby = (height + 15) / 16;

    if (!qf->block_cols && !qf->block_rows)
        return 0;

    if (qf->block_cols != mbx || qf->block_rows != mby) {
        av_log(log_ctx, AV_LOG_ERROR,
               "%s: the qpfile has %ux%u blocks per frame, the %dx%d video has %ux%u "
               "macroblocks.\n", encoder, qf->block_cols, qf->block_rows,
               width, height, mbx, mby);
        return AVERROR(EINVAL);
    }
    return 0;
}

/* Free every frame's offset map and the frame array, and reset the counters. */
void ff_qpfile_free(FFQpFile *qpf)
{
    if (!qpf)
        return;
    if (qpf->frames) {
        for (unsigned i = 0; i < qpf->frame_count; i++)
            av_freep(&qpf->frames[i].block_offsets);
        av_freep(&qpf->frames);
    }
    qpf->frame_count = 0;
    qpf->block_cols  = 0;
    qpf->block_rows  = 0;
}

const FFQpFrame *ff_qpfile_frame_for_index(const FFQpFile *qf,
                                           unsigned frame_index)
{
    unsigned low = 0, high;

    if (!qf || !qf->frame_count)
        return NULL;
    high = qf->frame_count;
    while (low < high) {
        unsigned mid = low + (high - low) / 2;

        if (qf->frames[mid].frame_index <= frame_index)
            low = mid + 1;
        else
            high = mid;
    }
    return low ? &qf->frames[low - 1] : NULL;
}

/* Scan distinct offsets, their minimum, and their maximum into seen. */
static void qpfile_histogram(const int8_t *vals, size_t n, uint8_t *seen,
                             int *min_off, int *max_off, unsigned *distinct)
{
    for (size_t i = 0; i < n; i++)
        seen[vals[i] - QPFILE_DELTA_MIN] = 1;
    for (int v = QPFILE_DELTA_MIN; v <= QPFILE_DELTA_MAX; v++) {
        if (!seen[v - QPFILE_DELTA_MIN])
            continue;
        *min_off = FFMIN(*min_off, v);
        *max_off = v;
        (*distinct)++;
    }
}

/* Select a representative palette of up to max_segments QP offsets. */
unsigned ff_qpfile_palette(const int8_t *vals, size_t n, int *seg_values,
                           unsigned max_segments)
{
    uint8_t seen[QPFILE_LUT_SIZE] = { 0 };
    int min_off = QPFILE_DELTA_MAX, max_off = QPFILE_DELTA_MIN;
    unsigned distinct = 0;

    if (!n || max_segments < 2)
        return 0;
    qpfile_histogram(vals, n, seen, &min_off, &max_off, &distinct);
    if (distinct <= max_segments) {
        unsigned k = 0;

        for (int v = min_off; v <= max_off; v++)
            if (seen[v - QPFILE_DELTA_MIN])
                seg_values[k++] = v;
        return k;
    }
    for (unsigned i = 0; i < max_segments; i++)
        seg_values[i] = min_off +
            (int)((unsigned)(max_off - min_off) * i / (max_segments - 1u));
    return max_segments;
}

/* Fill seg_values with the palette of the frame's block offsets. */
unsigned ff_qpfile_segment_values(const FFQpFrame *qf, int *seg_values,
                                  unsigned max_segments)
{
    if (!qf->block_offsets || !qf->block_cols || !qf->block_rows)
        return 0;
    return ff_qpfile_palette(qf->block_offsets,
                             (size_t)qf->block_rows * qf->block_cols,
                             seg_values, max_segments);
}

/* Return the index of the segment value nearest v, the first one on a tie. */
unsigned ff_qpfile_nearest_segment(int v, const int *seg_values, unsigned n_seg)
{
    unsigned best_idx = 0;
    int best_dist = INT_MAX;

    for (unsigned i = 0; i < n_seg; i++) {
        int d = FFABS(v - seg_values[i]);

        if (d < best_dist) {
            best_dist = d;
            best_idx  = i;
        }
    }
    return best_idx;
}

/* Fill lut with the nearest segment index for each valid offset. */
void ff_qpfile_segment_lut(uint8_t *lut, const int *seg_values, unsigned n_seg)
{
    for (int v = QPFILE_DELTA_MIN; v <= QPFILE_DELTA_MAX; v++)
        lut[v - QPFILE_DELTA_MIN] =
            (uint8_t)ff_qpfile_nearest_segment(v, seg_values, n_seg);
}
