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

#ifndef AVFILTER_YADIF_H
#define AVFILTER_YADIF_H

#include "libavutil/pixdesc.h"
#include "avfilter.h"

typedef struct {
    /**
     * 0: send 1 frame for each frame
     * 1: send 1 frame for each field
     * 2: like 0 but skips spatial interlacing check
     * 3: like 1 but skips spatial interlacing check
     */
    int mode;

    /**
     *  0: top field first
     *  1: bottom field first
     * -1: auto-detection
     */
    int parity;

    int frame_pending;

    /**
     *  0: deinterlace all frames
     *  1: only deinterlace frames marked as interlaced
     */
    int auto_enable;

    AVFilterBufferRef *cur;
    AVFilterBufferRef *next;
    AVFilterBufferRef *prev;
    AVFilterBufferRef *out;
    void (*filter_line)(uint8_t *dst,
                        uint8_t *prev, uint8_t *cur, uint8_t *next,
                        int w, int prefs, int mrefs, int parity, int mode);

    const AVPixFmtDescriptor *csp;
    int eof;
    uint8_t *temp_line;
    int temp_line_size;
} YADIFContext;

void ff_yadif_init_x86(YADIFContext *yadif);

#endif /* AVFILTER_YADIF_H */
