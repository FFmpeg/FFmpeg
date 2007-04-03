/*
 * Register all the formats and protocols
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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
#include "avformat.h"
#include "allformats.h"

#define REGISTER_MUXER(X,x) \
          if(ENABLE_##X##_MUXER)   av_register_output_format(&x##_muxer)
#define REGISTER_DEMUXER(X,x) \
          if(ENABLE_##X##_DEMUXER) av_register_input_format(&x##_demuxer)
#define REGISTER_MUXDEMUX(X,x)  REGISTER_MUXER(X,x); REGISTER_DEMUXER(X,x)

/* If you do not call this function, then you can select exactly which
   formats you want to support */

/**
 * Initialize libavformat and register all the (de)muxers and protocols.
 */
void av_register_all(void)
{
    static int inited = 0;

    if (inited != 0)
        return;
    inited = 1;

    avcodec_init();
    avcodec_register_all();

    REGISTER_DEMUXER (AAC, aac);
    REGISTER_MUXDEMUX(AC3, ac3);
    REGISTER_MUXER   (ADTS, adts);
    REGISTER_MUXDEMUX(AIFF, aiff);
    REGISTER_MUXDEMUX(AMR, amr);
    REGISTER_MUXDEMUX(ASF, asf);
    REGISTER_MUXER   (ASF_STREAM, asf_stream);
    REGISTER_MUXDEMUX(AU, au);
    REGISTER_MUXDEMUX(AUDIO, audio);
    REGISTER_MUXDEMUX(AVI, avi);
#ifdef CONFIG_AVISYNTH
    av_register_input_format(&avisynth_demuxer);
#endif
    REGISTER_DEMUXER (AVS, avs);
    REGISTER_MUXER   (CRC, crc);
    REGISTER_DEMUXER (DAUD, daud);
    REGISTER_DEMUXER (DC1394, dc1394);
    REGISTER_DEMUXER (DSICIN, dsicin);
    REGISTER_DEMUXER (DTS, dts);
    REGISTER_MUXDEMUX(DV, dv);
    REGISTER_DEMUXER (DV1394, dv1394);
    REGISTER_DEMUXER (DXA, dxa);
    REGISTER_DEMUXER (EA, ea);
    REGISTER_MUXDEMUX(FFM, ffm);
    REGISTER_MUXDEMUX(FLAC, flac);
    REGISTER_DEMUXER (FLIC, flic);
    REGISTER_MUXDEMUX(FLV, flv);
    REGISTER_DEMUXER (FOURXM, fourxm);
    REGISTER_MUXER   (FRAMECRC, framecrc);
    REGISTER_MUXDEMUX(GIF, gif);
    REGISTER_DEMUXER (GXF, gxf);
    REGISTER_MUXER   (GXF, gxf);
    REGISTER_MUXDEMUX(H261, h261);
    REGISTER_MUXDEMUX(H263, h263);
    REGISTER_MUXDEMUX(H264, h264);
    REGISTER_DEMUXER (IDCIN, idcin);
    REGISTER_MUXDEMUX(IMAGE2, image2);
    REGISTER_MUXDEMUX(IMAGE2PIPE, image2pipe);
    REGISTER_DEMUXER (INGENIENT, ingenient);
    REGISTER_DEMUXER (IPMOVIE, ipmovie);
    if (!ENABLE_NUT_DEMUXER) REGISTER_DEMUXER (LIBNUT, libnut);
    REGISTER_MUXER   (LIBNUT, libnut);
    REGISTER_MUXDEMUX(M4V, m4v);
    REGISTER_DEMUXER (MATROSKA, matroska);
    REGISTER_MUXDEMUX(MJPEG, mjpeg);
    REGISTER_DEMUXER (MM, mm);
    REGISTER_MUXDEMUX(MMF, mmf);
    REGISTER_MUXDEMUX(MOV, mov);
    REGISTER_MUXER   (MP2, mp2);
    REGISTER_MUXDEMUX(MP3, mp3);
    REGISTER_MUXER   (MP4, mp4);
    REGISTER_DEMUXER (MPC, mpc);
    REGISTER_MUXER   (MPEG1SYSTEM, mpeg1system);
    REGISTER_MUXER   (MPEG1VCD, mpeg1vcd);
    REGISTER_MUXER   (MPEG1VIDEO, mpeg1video);
    REGISTER_MUXER   (MPEG2DVD, mpeg2dvd);
    REGISTER_MUXER   (MPEG2SVCD, mpeg2svcd);
    REGISTER_MUXER   (MPEG2VIDEO, mpeg2video);
    REGISTER_MUXER   (MPEG2VOB, mpeg2vob);
    REGISTER_DEMUXER (MPEGPS, mpegps);
    REGISTER_MUXDEMUX(MPEGTS, mpegts);
    REGISTER_DEMUXER (MPEGVIDEO, mpegvideo);
    REGISTER_MUXER   (MPJPEG, mpjpeg);
    REGISTER_DEMUXER (MTV, mtv);
    REGISTER_DEMUXER (MXF, mxf);
    REGISTER_DEMUXER (NSV, nsv);
    REGISTER_MUXER   (NULL, null);
    REGISTER_DEMUXER (NUT, nut);
    REGISTER_DEMUXER (NUV, nuv);
    REGISTER_DEMUXER (OGG, ogg);
    REGISTER_MUXER   (OGG, ogg);
    REGISTER_MUXDEMUX(PCM_ALAW,  pcm_alaw);
    REGISTER_MUXDEMUX(PCM_MULAW, pcm_mulaw);
    REGISTER_MUXDEMUX(PCM_S16BE, pcm_s16be);
    REGISTER_MUXDEMUX(PCM_S16LE, pcm_s16le);
    REGISTER_MUXDEMUX(PCM_S8,    pcm_s8);
    REGISTER_MUXDEMUX(PCM_U16BE, pcm_u16be);
    REGISTER_MUXDEMUX(PCM_U16LE, pcm_u16le);
    REGISTER_MUXDEMUX(PCM_U8,    pcm_u8);
    REGISTER_MUXER   (PSP, psp);
    REGISTER_MUXDEMUX(RAWVIDEO, rawvideo);
    REGISTER_MUXDEMUX(RM, rm);
    REGISTER_DEMUXER (ROQ, roq);
    REGISTER_DEMUXER (REDIR, redir);
    REGISTER_MUXER   (RTP, rtp);
    REGISTER_DEMUXER (RTSP, rtsp);
    REGISTER_DEMUXER (SDP, sdp);
#ifdef CONFIG_NETWORK
    av_register_rtp_dynamic_payload_handlers();
#endif
    REGISTER_DEMUXER (SEGAFILM, segafilm);
    REGISTER_DEMUXER (SHORTEN, shorten);
    REGISTER_DEMUXER (SMACKER, smacker);
    REGISTER_DEMUXER (SOL, sol);
    REGISTER_DEMUXER (STR, str);
    REGISTER_MUXDEMUX(SWF, swf);
    REGISTER_MUXER   (TG2, tg2);
    REGISTER_MUXER   (TGP, tgp);
    REGISTER_DEMUXER (THP, thp);
    REGISTER_DEMUXER (TIERTEXSEQ, tiertexseq);
    REGISTER_DEMUXER (TTA, tta);
    REGISTER_DEMUXER (V4L2, v4l2);
    REGISTER_DEMUXER (VC1, vc1);
    REGISTER_DEMUXER (VIDEO_GRAB_BKTR, video_grab_bktr);
    REGISTER_DEMUXER (VIDEO_GRAB_V4L, video_grab_v4l);
    REGISTER_DEMUXER (VMD, vmd);
    REGISTER_MUXDEMUX(VOC, voc);
    REGISTER_MUXDEMUX(WAV, wav);
    REGISTER_DEMUXER (WC3, wc3);
    REGISTER_DEMUXER (WSAUD, wsaud);
    REGISTER_DEMUXER (WSVQA, wsvqa);
    REGISTER_DEMUXER (WV, wv);
    REGISTER_DEMUXER (X11_GRAB_DEVICE, x11_grab_device);
    REGISTER_MUXDEMUX(YUV4MPEGPIPE, yuv4mpegpipe);

#ifdef CONFIG_PROTOCOLS
    /* file protocols */
    register_protocol(&file_protocol);
    register_protocol(&pipe_protocol);
#ifdef CONFIG_NETWORK
    register_protocol(&udp_protocol);
    register_protocol(&rtp_protocol);
    register_protocol(&tcp_protocol);
    register_protocol(&http_protocol);
#endif
#endif
}
