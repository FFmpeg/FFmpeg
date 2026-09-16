/*
 * MPEG Audio header decoder
 * Copyright (c) 2001, 2002 Fabrice Bellard
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
 * MPEG Audio header decoder.
 */

#include <string.h>

#include "libavutil/error.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"

#include "mpegaudio.h"
#include "mpegaudiodata.h"
#include "mpegaudiodecheader.h"


int ff_mpegaudio_decode_header(MPADecodeHeader2 *s, uint32_t header)
{
    int sample_rate, frame_size, mpeg25, padding;
    int sample_rate_index, bitrate_index;
    int ret;

    ret = ff_mpa_check_header(header);
    if (ret < 0)
        return ret;

    memset(s, 0, sizeof(*s));

    if (header & (1<<20)) {
        s->lsf = (header & (1<<19)) ? 0 : 1;
        mpeg25 = 0;
    } else {
        s->lsf = 1;
        mpeg25 = 1;
    }

    s->layer = 4 - ((header >> 17) & 3);
    /* extract frequency */
    sample_rate_index = (header >> 10) & 3;
    if (sample_rate_index >= FF_ARRAY_ELEMS(ff_mpa_freq_tab))
        sample_rate_index = 0;
    sample_rate = ff_mpa_freq_tab[sample_rate_index] >> (s->lsf + mpeg25);
    sample_rate_index += 3 * (s->lsf + mpeg25);
    s->sample_rate_index = sample_rate_index;
    s->error_protection = ((header >> 16) & 1) ^ 1;
    s->sample_rate = sample_rate;

    bitrate_index = (header >> 12) & 0xf;
    padding = (header >> 9) & 1;
    s->bitrate_index = bitrate_index;
    s->padding = padding;
    s->private_bit = (header >> 8) & 1;
    s->mode = (header >> 6) & 3;
    s->mode_ext = (header >> 4) & 3;
    s->copyright = (header >> 3) & 1;
    s->original = (header >> 2) & 1;
    s->emphasis = header & 3;

    if (s->mode == MPA_MONO)
        s->nb_channels = 1;
    else
        s->nb_channels = 2;

    if (bitrate_index != 0) {
        frame_size = ff_mpa_bitrate_tab[s->lsf][s->layer - 1][bitrate_index];
        s->bit_rate = frame_size * 1000;
        switch(s->layer) {
        case 1:
            frame_size = (frame_size * 12000) / sample_rate;
            frame_size = (frame_size + padding) * 4;
            break;
        case 2:
            frame_size = (frame_size * 144000) / sample_rate;
            frame_size += padding;
            break;
        default:
        case 3:
            frame_size = (frame_size * 144000) / (sample_rate << s->lsf);
            frame_size += padding;
            break;
        }
        s->frame_size = frame_size;
    } else {
        /* if no frame size computed, signal it */
        return 1;
    }

#if defined(DEBUG)
    ff_dlog(NULL, "layer%d, %d Hz, %d kbits/s, ",
           s->layer, s->sample_rate, s->bit_rate);
    if (s->nb_channels == 2) {
        if (s->layer == 3) {
            if (s->mode_ext & MODE_EXT_MS_STEREO)
                ff_dlog(NULL, "ms-");
            if (s->mode_ext & MODE_EXT_I_STEREO)
                ff_dlog(NULL, "i-");
        }
        ff_dlog(NULL, "stereo");
    } else {
        ff_dlog(NULL, "mono");
    }
    ff_dlog(NULL, "\n");
#endif
    return 0;
}

int avpriv_mpegaudio_decode_header2(MPADecodeHeader2 **phdr, uint32_t header)
{
    MPADecodeHeader2 *hdr = *phdr;
    int ret;

    if (!hdr) {
        ret = ff_mpa_check_header(header);
        if (ret < 0)
            return ret;
        hdr = av_mallocz(sizeof(*hdr));
        if (!hdr)
            return AVERROR(ENOMEM);
    }

    ret = ff_mpegaudio_decode_header(hdr, header);
    if (ret < 0)
        return ret;

    *phdr = hdr;
    return ret;
}

#if FF_MPEGAUDIO_LEGACY_DECODE_HEADER
int avpriv_mpegaudio_decode_header(MPADecodeHeader *s, uint32_t header)
{
    MPADecodeHeader2 hdr = { 0 };
    int ret = ff_mpegaudio_decode_header(&hdr, header);

    if (ret < 0)
        return ret;

    /* callers in other libraries allocate the smaller structure, so only
       its fields may be written */
    s->frame_size        = hdr.frame_size;
    s->error_protection  = hdr.error_protection;
    s->layer             = hdr.layer;
    s->sample_rate       = hdr.sample_rate;
    s->sample_rate_index = hdr.sample_rate_index;
    s->bit_rate          = hdr.bit_rate;
    s->nb_channels       = hdr.nb_channels;
    s->mode              = hdr.mode;
    s->mode_ext          = hdr.mode_ext;
    s->lsf               = hdr.lsf;

    return ret;
}
#endif

int ff_mpa_decode_header(uint32_t head, int *sample_rate, int *channels, int *frame_size, int *bit_rate, enum AVCodecID *codec_id)
{
    MPADecodeHeader2 s1, *s = &s1;

    if (ff_mpegaudio_decode_header(s, head) != 0) {
        return -1;
    }

    switch(s->layer) {
    case 1:
        *codec_id = AV_CODEC_ID_MP1;
        *frame_size = 384;
        break;
    case 2:
        *codec_id = AV_CODEC_ID_MP2;
        *frame_size = 1152;
        break;
    default:
    case 3:
        if (*codec_id != AV_CODEC_ID_MP3ADU)
            *codec_id = AV_CODEC_ID_MP3;
        if (s->lsf)
            *frame_size = 576;
        else
            *frame_size = 1152;
        break;
    }

    *sample_rate = s->sample_rate;
    *channels = s->nb_channels;
    *bit_rate = s->bit_rate;
    return s->frame_size;
}
