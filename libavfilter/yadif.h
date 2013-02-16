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

enum YADIFMode {
    YADIF_MODE_SEND_FRAME           = 0, ///< send 1 frame for each frame
    YADIF_MODE_SEND_FIELD           = 1, ///< send 1 frame for each field
    YADIF_MODE_SEND_FRAME_NOSPATIAL = 2, ///< send 1 frame for each frame but skips spatial interlacing check
    YADIF_MODE_SEND_FIELD_NOSPATIAL = 3, ///< send 1 frame for each field but skips spatial interlacing check
};

enum YADIFParity {
    YADIF_PARITY_TFF  =  0, ///< top field first
    YADIF_PARITY_BFF  =  1, ///< bottom field first
    YADIF_PARITY_AUTO = -1, ///< auto detection
};

enum YADIFDeint {
    YADIF_DEINT_ALL        = 0, ///< deinterlace all frames
    YADIF_DEINT_INTERLACED = 1, ///< only deinterlace frames marked as interlaced
};

typedef struct YADIFContext {
    const AVClass *class;

    enum YADIFMode   mode;
    enum YADIFParity parity;
    enum YADIFDeint  deint;

    int frame_pending;

    AVFilterBufferRef *cur;
    AVFilterBufferRef *next;
    AVFilterBufferRef *prev;
    AVFilterBufferRef *out;

    /**
     * Required alignment for filter_line
     */
    int req_align;
    void (*filter_line)(void *dst,
                        void *prev, void *cur, void *next,
                        int w, int prefs, int mrefs, int parity, int mode);
    void (*filter_edges)(void *dst, void *prev, void *cur, void *next,
                         int w, int prefs, int mrefs, int parity, int mode,
                         int l_edge);

    const AVPixFmtDescriptor *csp;
    int eof;
    uint8_t *temp_line;
    int temp_line_size;
} YADIFContext;

void ff_yadif_init_x86(YADIFContext *yadif);

#endif /* AVFILTER_YADIF_H */
