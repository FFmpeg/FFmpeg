/*
 * sndio play and grab interface
 * Copyright (c) 2010 Jacob Meuser
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>
#include <sndio.h>

#include "libavformat/avformat.h"

#include "sndio_common.h"

static inline void movecb(void *addr, int delta)
{
    SndioData *s = addr;

    s->hwpos += delta * s->channels * s->bps;
}

av_cold int ff_sndio_open(AVFormatContext *s1, int is_output,
                          const char *audio_device)
{
    SndioData *s = s1->priv_data;
    struct sio_hdl *hdl;
    struct sio_par par;

    hdl = sio_open(audio_device, is_output ? SIO_PLAY : SIO_REC, 0);
    if (!hdl) {
        av_log(s1, AV_LOG_ERROR, "Could not open sndio device\n");
        return AVERROR(EIO);
    }

    sio_initpar(&par);

    par.bits = 16;
    par.sig  = 1;
    par.le   = SIO_LE_NATIVE;

    if (is_output)
        par.pchan = s->channels;
    else
        par.rchan = s->channels;
    par.rate = s->sample_rate;

    if (!sio_setpar(hdl, &par) || !sio_getpar(hdl, &par)) {
        av_log(s1, AV_LOG_ERROR, "Impossible to set sndio parameters, "
               "channels: %d sample rate: %d\n", s->channels, s->sample_rate);
        goto fail;
    }

    if (par.bits != 16 || par.sig != 1 ||
        (is_output  && (par.pchan != s->channels)) ||
        (!is_output && (par.rchan != s->channels)) ||
        (par.rate != s->sample_rate)) {
        av_log(s1, AV_LOG_ERROR, "Could not set appropriate sndio parameters, "
               "channels: %d sample rate: %d\n", s->channels, s->sample_rate);
        goto fail;
    }

    s->buffer_size = par.round * par.bps *
                     (is_output ? par.pchan : par.rchan);

    if (is_output) {
        s->buffer = av_malloc(s->buffer_size);
        if (!s->buffer) {
            av_log(s1, AV_LOG_ERROR, "Could not allocate buffer\n");
            goto fail;
        }
    }

    s->codec_id    = par.le ? CODEC_ID_PCM_S16LE : CODEC_ID_PCM_S16BE;
    s->channels    = is_output ? par.pchan : par.rchan;
    s->sample_rate = par.rate;
    s->bps         = par.bps;

    sio_onmove(hdl, movecb, s);

    if (!sio_start(hdl)) {
        av_log(s1, AV_LOG_ERROR, "Could not start sndio\n");
        goto fail;
    }

    s->hdl = hdl;

    return 0;

fail:
    av_freep(&s->buffer);

    if (hdl)
        sio_close(hdl);

    return AVERROR(EIO);
}

int ff_sndio_close(SndioData *s)
{
    av_freep(&s->buffer);

    if (s->hdl)
        sio_close(s->hdl);

    return 0;
}
