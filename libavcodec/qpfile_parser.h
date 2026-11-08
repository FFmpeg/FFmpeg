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
 * Shared parser for the per-frame QP-offset file format. Consumed by libx264,
 * libsvtav1, and libaom-av1 encoder adapters via the new -qpfile
 * AVOption.
 *
 * Keep the parser shared across encoder adapters so future
 * encoder additions inherit the same format handling and
 * validation rules.
 *
 * The parsed block offsets are QP deltas for libx264, deltas on libaom's
 * 0..63 quantizer scale for libaom-av1 (libaom converts each to a
 * quantizer index delta, 12 becoming 48) and AV1 quantizer index deltas
 * for libsvtav1.
 */

#ifndef AVCODEC_QPFILE_PARSER_H
#define AVCODEC_QPFILE_PARSER_H

#include <stddef.h>
#include <stdint.h>

#define QPFILE_DELTA_MIN (-12)
#define QPFILE_DELTA_MAX (12)
#define QPFILE_LUT_SIZE (QPFILE_DELTA_MAX - QPFILE_DELTA_MIN + 1)

typedef struct FFQpFrame {
    unsigned int frame_index;
    int8_t      *block_offsets;
    unsigned int block_cols;
    unsigned int block_rows;
} FFQpFrame;

typedef struct FFQpFile {
    FFQpFrame *frames;
    unsigned int     frame_count;
    unsigned int     block_cols;
    unsigned int     block_rows;
} FFQpFile;

/** Load a per-frame QP-offset file into out, logging errors to log_ctx. */
int ff_qpfile_load(void *log_ctx, const char *path, FFQpFile *out);
/** Verify that the QP file grid matches the video macroblock dimensions. */
int ff_qpfile_check_grid(void *log_ctx, const char *encoder, const FFQpFile *qf,
                         int width, int height);
/** Free all memory allocated inside an FFQpFile structure. */
void ff_qpfile_free(FFQpFile *qpf);
/** Look up the QP frame record matching or preceding the given frame index. */
const FFQpFrame *ff_qpfile_frame_for_index(const FFQpFile *qf,
                                           unsigned frame_index);
/** Fill seg_values with the palette (see ff_qpfile_palette) of the frame's
 *  block offsets, returning its size, or 0 without offsets. */
unsigned ff_qpfile_segment_values(const FFQpFrame *qf, int *seg_values,
                                  unsigned max_segments);
/** Return the index of the segment value nearest v, the first one on a tie. */
unsigned ff_qpfile_nearest_segment(int v, const int *seg_values, unsigned n_seg);
/** Fill seg_values, ascending, with each distinct value of the n offsets in
 *  vals when there are at most max_segments (at least 2) of them, else with
 *  max_segments evenly spaced values over their range; return the count,
 *  0 when n is 0. Every value must lie in QPFILE_DELTA_MIN..QPFILE_DELTA_MAX. */
unsigned ff_qpfile_palette(const int8_t *vals, size_t n, int *seg_values,
                           unsigned max_segments);
/** Fill lut[QPFILE_LUT_SIZE] with the nearest segment index of each offset
 *  from QPFILE_DELTA_MIN to QPFILE_DELTA_MAX. */
void ff_qpfile_segment_lut(uint8_t *lut, const int *seg_values, unsigned n_seg);

#endif /* AVCODEC_QPFILE_PARSER_H */
