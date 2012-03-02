/*
 * ALSA input and output
 * Copyright (c) 2007 Luca Abeni ( lucabe72 email it )
 * Copyright (c) 2007 Benoit Fouet ( benoit fouet free fr )
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

/**
 * @file
 * ALSA input and output: definitions and structures
 * @author Luca Abeni ( lucabe72 email it )
 * @author Benoit Fouet ( benoit fouet free fr )
 */

#ifndef AVDEVICE_ALSA_AUDIO_H
#define AVDEVICE_ALSA_AUDIO_H

#include <alsa/asoundlib.h>
#include "config.h"
#include "libavutil/log.h"
#include "timefilter.h"
#include "avdevice.h"

/* XXX: we make the assumption that the soundcard accepts this format */
/* XXX: find better solution with "preinit" method, needed also in
        other formats */
#define DEFAULT_CODEC_ID AV_NE(CODEC_ID_PCM_S16BE, CODEC_ID_PCM_S16LE)

typedef void (*ff_reorder_func)(const void *, void *, int);

#define ALSA_BUFFER_SIZE_MAX 65536

typedef struct {
    AVClass *class;
    snd_pcm_t *h;
    int frame_size;  ///< bytes per sample * channels
    int period_size; ///< preferred size for reads and writes, in frames
    int sample_rate; ///< sample rate set by user
    int channels;    ///< number of channels set by user
    int last_period;
    TimeFilter *timefilter;
    void (*reorder_func)(const void *, void *, int);
    void *reorder_buf;
    int reorder_buf_size; ///< in frames
} AlsaData;

/**
 * Open an ALSA PCM.
 *
 * @param s media file handle
 * @param mode either SND_PCM_STREAM_CAPTURE or SND_PCM_STREAM_PLAYBACK
 * @param sample_rate in: requested sample rate;
 *                    out: actually selected sample rate
 * @param channels number of channels
 * @param codec_id in: requested CodecID or CODEC_ID_NONE;
 *                 out: actually selected CodecID, changed only if
 *                 CODEC_ID_NONE was requested
 *
 * @return 0 if OK, AVERROR_xxx on error
 */
int ff_alsa_open(AVFormatContext *s, snd_pcm_stream_t mode,
                 unsigned int *sample_rate,
                 int channels, enum CodecID *codec_id);

/**
 * Close the ALSA PCM.
 *
 * @param s1 media file handle
 *
 * @return 0
 */
int ff_alsa_close(AVFormatContext *s1);

/**
 * Try to recover from ALSA buffer underrun.
 *
 * @param s1 media file handle
 * @param err error code reported by the previous ALSA call
 *
 * @return 0 if OK, AVERROR_xxx on error
 */
int ff_alsa_xrun_recover(AVFormatContext *s1, int err);

int ff_alsa_extend_reorder_buf(AlsaData *s, int size);

#endif /* AVDEVICE_ALSA_AUDIO_H */
