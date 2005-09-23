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
    static int inited = 0;
    
    if (inited != 0)
        return;
    inited = 1;

    avcodec_init();
    avcodec_register_all();

    mpegps_init();
    mpegts_init();
#ifdef CONFIG_MUXERS
    crc_init();
    img_init();
    img2_init();
#endif //CONFIG_MUXERS
    raw_init();
    mp3_init();
    rm_init();
    asf_init();
#ifdef CONFIG_MUXERS
    avienc_init();
#endif //CONFIG_MUXERS
    avidec_init();
    ff_wav_init();
    ff_mmf_init();
    swf_init();
    au_init();
#ifdef CONFIG_MUXERS
    gif_init();
#endif //CONFIG_MUXERS
    mov_init();
#ifdef CONFIG_MUXERS
    movenc_init();
    jpeg_init();
#endif //CONFIG_MUXERS
    ff_dv_init();
    fourxm_init();
#ifdef CONFIG_MUXERS
    flvenc_init();
#endif //CONFIG_MUXERS
    flvdec_init();
    str_init();
    roq_init();
    ipmovie_init();
    wc3_init();
    westwood_init();
    film_init();
    idcin_init();
    flic_init();
    vmd_init();

#if defined(AMR_NB) || defined(AMR_NB_FIXED) || defined(AMR_WB)
    amr_init();
#endif
    yuv4mpeg_init();

    ogg_init();
#ifdef CONFIG_LIBOGG
    libogg_init();
#endif

    ffm_init();
#if defined(CONFIG_VIDEO4LINUX) || defined(CONFIG_BKTR)
    video_grab_init();
#endif
#if defined(CONFIG_AUDIO_OSS) || defined(CONFIG_AUDIO_BEOS)
    audio_init();
#endif

#ifdef CONFIG_DV1394
    dv1394_init();
#endif

#ifdef CONFIG_DC1394
    dc1394_init();
#endif

    nut_init();
    matroska_init();
    sol_init();
    ea_init();
    nsvdec_init();
    daud_init();

#ifdef CONFIG_MUXERS
    /* image formats */
#if 0
    av_register_image_format(&pnm_image_format);
    av_register_image_format(&pbm_image_format);
    av_register_image_format(&pgm_image_format);
    av_register_image_format(&ppm_image_format);
    av_register_image_format(&pam_image_format);
    av_register_image_format(&pgmyuv_image_format);
    av_register_image_format(&yuv_image_format);
#ifdef CONFIG_ZLIB
    av_register_image_format(&png_image_format);
#endif
    av_register_image_format(&jpeg_image_format);
#endif
    av_register_image_format(&gif_image_format);  
//    av_register_image_format(&sgi_image_format); heap corruption, dont enable
#endif //CONFIG_MUXERS

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
