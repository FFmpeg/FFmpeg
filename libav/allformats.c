/*
 * Register all the formats and protocols
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avformat.h"

/* If you do not call this function, then you can select exactly which
   formats you want to support */

/**
 * Initialize libavcodec and register all the codecs and formats.
 */
void av_register_all(void)
{
    avcodec_init();
    avcodec_register_all();

    mpegps_init();
    mpegts_init();
    crc_init();
    img_init();
    raw_init();
    rm_init();
    asf_init();
    avienc_init();
    avidec_init();
    wav_init();
    swf_init();
    au_init();
    gif_init();
    mov_init();
    jpeg_init();

#ifdef CONFIG_VORBIS
    ogg_init();
#endif

#ifndef CONFIG_WIN32
    ffm_init();
#endif
#ifdef CONFIG_VIDEO4LINUX
    video_grab_init();
#endif
#ifdef CONFIG_AUDIO_OSS
    audio_init();
#endif

    /* file protocols */
    register_protocol(&file_protocol);
    register_protocol(&pipe_protocol);
#ifdef CONFIG_NETWORK
    rtsp_init();
    rtp_init();
    register_protocol(&udp_protocol);
    register_protocol(&rtp_protocol);
    register_protocol(&tcp_protocol);
    register_protocol(&http_protocol);
#endif
}
