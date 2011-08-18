/*
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

#ifndef AVFILTER_ASINK_ABUFFER_H
#define AVFILTER_ASINK_ABUFFER_H

/**
 * @file
 * audio buffer sink API
 */

#include "avfilter.h"

typedef struct {
    const enum AVSampleFormat *sample_fmts; ///< list of allowed sample formats,  terminated by -1
    const int64_t *channel_layouts;         ///< list of allowed channel layouts, terminated by -1
    const int *packing_fmts;                ///< list of allowed packing formats, terminated by -1
} ABufferSinkContext;


/**
 * Get an audio buffer from abuffersink and put it in samplesref.
 *
 * @param abuffersink pointer to an abuffersink context
 * @param flags unused
 * @return >= 0 in case of success, a negative AVERROR code in case of failure
 */
int av_asink_abuffer_get_audio_buffer_ref(AVFilterContext *abuffersink,
                                          AVFilterBufferRef **samplesref,
                                          int av_unused flags);

#endif /* AVFILTER_ASINK_ABUFFER_H */
