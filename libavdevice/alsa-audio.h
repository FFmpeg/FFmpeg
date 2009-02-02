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
 * @file libavdevice/alsa-audio.h
 * ALSA input and output: definitions and structures
 * @author Luca Abeni ( lucabe72 email it )
 * @author Benoit Fouet ( benoit fouet free fr )
 */

#ifndef AVDEVICE_ALSA_AUDIO_H
#define AVDEVICE_ALSA_AUDIO_H

#include <alsa/asoundlib.h>
#include "config.h"
#include "libavformat/avformat.h"

/* XXX: we make the assumption that the soundcard accepts this format */
/* XXX: find better solution with "preinit" method, needed also in
        other formats */
#ifdef WORDS_BIGENDIAN
#define DEFAULT_CODEC_ID CODEC_ID_PCM_S16BE
#else
#define DEFAULT_CODEC_ID CODEC_ID_PCM_S16LE
#endif

typedef struct {
    snd_pcm_t *h;
    int frame_size;  ///< preferred size for reads and writes
    int period_size; ///< bytes per sample * channels
} AlsaData;

/**
 * Opens an ALSA PCM.
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
int ff_alsa_open(AVFormatContext *s, int mode, unsigned int *sample_rate,
                 int channels, int *codec_id);

/**
 * Closes the ALSA PCM.
 *
 * @param s1 media file handle
 *
 * @return 0
 */
int ff_alsa_close(AVFormatContext *s1);

/**
 * Tries to recover from ALSA buffer underrun.
 *
 * @param s1 media file handle
 * @param err error code reported by the previous ALSA call
 *
 * @return 0 if OK, AVERROR_xxx on error
 */
int ff_alsa_xrun_recover(AVFormatContext *s1, int err);

#endif /* AVDEVICE_ALSA_AUDIO_H */
