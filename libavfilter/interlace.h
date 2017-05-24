/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * progressive to interlaced content filter, inspired by heavy debugging of
 * tinterlace filter.
 */

#ifndef AVFILTER_INTERLACE_H
#define AVFILTER_INTERLACE_H

#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

enum ScanMode {
    MODE_TFF = 0,
    MODE_BFF = 1,
};

enum FieldType {
    FIELD_UPPER = 0,
    FIELD_LOWER = 1,
};

enum VLPFilter {
    VLPF_OFF = 0,
    VLPF_LIN = 1,
    VLPF_CMP = 2,
};

typedef struct InterlaceContext {
    const AVClass *class;
    enum ScanMode scan;    // top or bottom field first scanning
    int lowpass;           // enable or disable low pass filtering
    AVFrame *cur, *next;   // the two frames from which the new one is obtained
    void (*lowpass_line)(uint8_t *dstp, ptrdiff_t linesize, const uint8_t *srcp,
                         ptrdiff_t mref, ptrdiff_t pref);
} InterlaceContext;

void ff_interlace_init_x86(InterlaceContext *interlace);

#endif /* AVFILTER_INTERLACE_H */
