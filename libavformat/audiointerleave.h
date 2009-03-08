/*
 * audio interleaving prototypes and declarations
 *
 * Copyright (c) 2009 Baptiste Coudurier <baptiste dot coudurier at gmail dot com>
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

#ifndef AVFORMAT_AUDIOINTERLEAVE_H
#define AVFORMAT_AUDIOINTERLEAVE_H

#include "libavutil/fifo.h"
#include "avformat.h"

typedef struct {
    AVFifoBuffer *fifo;
    unsigned fifo_size;           ///< size of currently allocated FIFO
    uint64_t dts;                 ///< current dts
    int sample_size;              ///< size of one sample all channels included
    const int *samples_per_frame; ///< must be 0-terminated
    const int *samples;           ///< current samples per frame, pointer to samples_per_frame
    AVRational time_base;         ///< time base of output audio packets
} AudioInterleaveContext;

int ff_audio_interleave_init(AVFormatContext *s, const int *samples_per_frame, AVRational time_base);
void ff_audio_interleave_close(AVFormatContext *s);

int ff_interleave_compare_dts(AVFormatContext *s, AVPacket *next, AVPacket *pkt);
/**
 * Rechunk audio PCM packets per AudioInterleaveContext->samples_per_frame
 * and interleave them correctly.
 * The first element of AVStream->priv_data must be AudioInterleaveContext
 * when using this function.
 *
 * @param get_packet function will output a packet when streams are correctly interleaved.
 * @param compare_ts function will compare AVPackets and decide interleaving order.
 */
int ff_audio_rechunk_interleave(AVFormatContext *s, AVPacket *out, AVPacket *pkt, int flush,
                        int (*get_packet)(AVFormatContext *, AVPacket *, AVPacket *, int),
                        int (*compare_ts)(AVFormatContext *, AVPacket *, AVPacket *));

#endif /* AVFORMAT_AUDIOINTERLEAVE_H */
