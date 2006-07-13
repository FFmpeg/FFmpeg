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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include "avformat.h"
#include "allformats.h"

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

#ifdef CONFIG_FOURXM_DEMUXER
    av_register_input_format(&fourxm_demuxer);
#endif
#ifdef CONFIG_ADTS_MUXER
    av_register_output_format(&adts_muxer);
#endif
#ifdef CONFIG_AIFF_DEMUXER
    av_register_input_format(&aiff_demuxer);
#endif
#ifdef CONFIG_AIFF_MUXER
    av_register_output_format(&aiff_muxer);
#endif
#ifdef CONFIG_AMR_DEMUXER
    av_register_input_format(&amr_demuxer);
#endif
#ifdef CONFIG_AMR_MUXER
    av_register_output_format(&amr_muxer);
#endif
#ifdef CONFIG_ASF_DEMUXER
    av_register_input_format(&asf_demuxer);
#endif
#ifdef CONFIG_ASF_MUXER
    av_register_output_format(&asf_muxer);
#endif
#ifdef CONFIG_ASF_STREAM_MUXER
    av_register_output_format(&asf_stream_muxer);
#endif
#ifdef CONFIG_AU_DEMUXER
    av_register_input_format(&au_demuxer);
#endif
#ifdef CONFIG_AU_MUXER
    av_register_output_format(&au_muxer);
#endif
#if defined(CONFIG_AUDIO_OSS) || defined(CONFIG_AUDIO_BEOS)
#ifdef CONFIG_AUDIO_DEMUXER
    av_register_input_format(&audio_demuxer);
#endif
#ifdef CONFIG_AUDIO_MUXER
    av_register_output_format(&audio_muxer);
#endif
#endif /* CONFIG_AUDIO_OSS || CONFIG_AUDIO_BEOS */
#ifdef CONFIG_AVI_DEMUXER
    av_register_input_format(&avi_demuxer);
#endif
#ifdef CONFIG_AVI_MUXER
    av_register_output_format(&avi_muxer);
#endif
#ifdef CONFIG_AVS_DEMUXER
    av_register_input_format(&avs_demuxer);
#endif
#ifdef CONFIG_CRC_MUXER
    av_register_output_format(&crc_muxer);
#endif
#ifdef CONFIG_FRAMECRC_MUXER
    av_register_output_format(&framecrc_muxer);
#endif
#ifdef CONFIG_DAUD_DEMUXER
    av_register_input_format(&daud_demuxer);
#endif
#ifdef CONFIG_DC1394
#ifdef CONFIG_DC1394_DEMUXER
    av_register_input_format(&dc1394_demuxer);
#endif
#endif /* CONFIG_DC1394 */
#ifdef CONFIG_DV1394
#ifdef CONFIG_DV1394_DEMUXER
    av_register_input_format(&dv1394_demuxer);
#endif
#endif /* CONFIG_DV1394 */
#ifdef CONFIG_DV_DEMUXER
    av_register_input_format(&dv_demuxer);
#endif
#ifdef CONFIG_DV_MUXER
    av_register_output_format(&dv_muxer);
#endif
#ifdef CONFIG_EA_DEMUXER
    av_register_input_format(&ea_demuxer);
#endif
#ifdef CONFIG_FFM_DEMUXER
    av_register_input_format(&ffm_demuxer);
#endif
#ifdef CONFIG_FFM_MUXER
    av_register_output_format(&ffm_muxer);
#endif
#ifdef CONFIG_FLIC_DEMUXER
    av_register_input_format(&flic_demuxer);
#endif
#ifdef CONFIG_FLV_DEMUXER
    av_register_input_format(&flv_demuxer);
#endif
#ifdef CONFIG_FLV_MUXER
    av_register_output_format(&flv_muxer);
#endif
#ifdef CONFIG_GIF_MUXER
    av_register_output_format(&gif_muxer);
#endif
#ifdef CONFIG_GIF_DEMUXER
    av_register_input_format(&gif_demuxer);
#endif
#ifdef CONFIG_GXF_DEMUXER
    av_register_input_format(&gxf_demuxer);
#endif
#ifdef CONFIG_IDCIN_DEMUXER
    av_register_input_format(&idcin_demuxer);
#endif
#ifdef CONFIG_ROQ_DEMUXER
    av_register_input_format(&roq_demuxer);
#endif
#ifdef CONFIG_IMAGE2_DEMUXER
    av_register_input_format(&image2_demuxer);
#endif
#ifdef CONFIG_IMAGE2PIPE_DEMUXER
    av_register_input_format(&image2pipe_demuxer);
#endif
#ifdef CONFIG_IMAGE2_MUXER
    av_register_output_format(&image2_muxer);
#endif
#ifdef CONFIG_IMAGE2PIPE_MUXER
    av_register_output_format(&image2pipe_muxer);
#endif
#ifdef CONFIG_IMAGE_DEMUXER
    av_register_input_format(&image_demuxer);
#endif
#ifdef CONFIG_IMAGEPIPE_DEMUXER
    av_register_input_format(&imagepipe_demuxer);
#endif
#ifdef CONFIG_IMAGE_MUXER
    av_register_output_format(&image_muxer);
#endif
#ifdef CONFIG_IMAGEPIPE_MUXER
    av_register_output_format(&imagepipe_muxer);
#endif
#ifdef CONFIG_IPMOVIE_DEMUXER
    av_register_input_format(&ipmovie_demuxer);
#endif
#ifdef CONFIG_MATROSKA_DEMUXER
    av_register_input_format(&matroska_demuxer);
#endif
#ifdef CONFIG_MM_DEMUXER
    av_register_input_format(&mm_demuxer);
#endif
#ifdef CONFIG_MMF_DEMUXER
    av_register_input_format(&mmf_demuxer);
#endif
#ifdef CONFIG_MMF_MUXER
    av_register_output_format(&mmf_muxer);
#endif
#ifdef CONFIG_MOV_DEMUXER
    av_register_input_format(&mov_demuxer);
#endif
#ifdef CONFIG_MOV_MUXER
    av_register_output_format(&mov_muxer);
#endif
#ifdef CONFIG_TGP_MUXER
    av_register_output_format(&tgp_muxer);
#endif
#ifdef CONFIG_MP4_MUXER
    av_register_output_format(&mp4_muxer);
#endif
#ifdef CONFIG_PSP_MUXER
    av_register_output_format(&psp_muxer);
#endif
#ifdef CONFIG_TG2_MUXER
    av_register_output_format(&tg2_muxer);
#endif
#ifdef CONFIG_MP3_DEMUXER
    av_register_input_format(&mp3_demuxer);
#endif
#ifdef CONFIG_MP2_MUXER
    av_register_output_format(&mp2_muxer);
#endif
#ifdef CONFIG_MP3_MUXER
    av_register_output_format(&mp3_muxer);
#endif
#ifdef CONFIG_MPEG1SYSTEM_MUXER
    av_register_output_format(&mpeg1system_muxer);
#endif
#ifdef CONFIG_MPEG1VCD_MUXER
    av_register_output_format(&mpeg1vcd_muxer);
#endif
#ifdef CONFIG_MPEG2VOB_MUXER
    av_register_output_format(&mpeg2vob_muxer);
#endif
#ifdef CONFIG_MPEG2SVCD_MUXER
    av_register_output_format(&mpeg2svcd_muxer);
#endif
#ifdef CONFIG_MPEG2DVD_MUXER
    av_register_output_format(&mpeg2dvd_muxer);
#endif
#ifdef CONFIG_MPEGPS_DEMUXER
    av_register_input_format(&mpegps_demuxer);
#endif
#ifdef CONFIG_MPEGTS_DEMUXER
    av_register_input_format(&mpegts_demuxer);
#endif
#ifdef CONFIG_MPEGTS_MUXER
    av_register_output_format(&mpegts_muxer);
#endif
#ifdef CONFIG_MPJPEG_MUXER
    av_register_output_format(&mpjpeg_muxer);
#endif
#ifdef CONFIG_NSV_DEMUXER
    av_register_input_format(&nsv_demuxer);
#endif
#ifdef CONFIG_NUT_DEMUXER
    av_register_input_format(&nut_demuxer);
#endif
#ifdef CONFIG_NUT_MUXER
    av_register_output_format(&nut_muxer);
#endif
#ifdef CONFIG_NUV_DEMUXER
    av_register_input_format(&nuv_demuxer);
#endif
#ifdef CONFIG_OGG_DEMUXER
    av_register_input_format(&ogg_demuxer);
#endif
#ifdef CONFIG_LIBOGG
#ifdef CONFIG_OGG_MUXER
    av_register_output_format(&ogg_muxer);
#endif
#endif /* CONFIG_LIBOGG */
#ifdef CONFIG_STR_DEMUXER
    av_register_input_format(&str_demuxer);
#endif
#ifdef CONFIG_SHORTEN_DEMUXER
    av_register_input_format(&shorten_demuxer);
#endif
#ifdef CONFIG_FLAC_DEMUXER
    av_register_input_format(&flac_demuxer);
#endif
#ifdef CONFIG_FLAC_MUXER
    av_register_output_format(&flac_muxer);
#endif
#ifdef CONFIG_AC3_DEMUXER
    av_register_input_format(&ac3_demuxer);
#endif
#ifdef CONFIG_AC3_MUXER
    av_register_output_format(&ac3_muxer);
#endif
#ifdef CONFIG_DTS_DEMUXER
    av_register_input_format(&dts_demuxer);
#endif
#ifdef CONFIG_AAC_DEMUXER
    av_register_input_format(&aac_demuxer);
#endif
#ifdef CONFIG_H261_DEMUXER
    av_register_input_format(&h261_demuxer);
#endif
#ifdef CONFIG_H261_MUXER
    av_register_output_format(&h261_muxer);
#endif
#ifdef CONFIG_H263_DEMUXER
    av_register_input_format(&h263_demuxer);
#endif
#ifdef CONFIG_H263_MUXER
    av_register_output_format(&h263_muxer);
#endif
#ifdef CONFIG_M4V_DEMUXER
    av_register_input_format(&m4v_demuxer);
#endif
#ifdef CONFIG_M4V_MUXER
    av_register_output_format(&m4v_muxer);
#endif
#ifdef CONFIG_H264_DEMUXER
    av_register_input_format(&h264_demuxer);
#endif
#ifdef CONFIG_H264_MUXER
    av_register_output_format(&h264_muxer);
#endif
#ifdef CONFIG_MPEGVIDEO_DEMUXER
    av_register_input_format(&mpegvideo_demuxer);
#endif
#ifdef CONFIG_MPEG1VIDEO_MUXER
    av_register_output_format(&mpeg1video_muxer);
#endif
#ifdef CONFIG_MPEG2VIDEO_MUXER
    av_register_output_format(&mpeg2video_muxer);
#endif
#ifdef CONFIG_MJPEG_DEMUXER
    av_register_input_format(&mjpeg_demuxer);
#endif
#ifdef CONFIG_INGENIENT_DEMUXER
    av_register_input_format(&ingenient_demuxer);
#endif
#ifdef CONFIG_MJPEG_MUXER
    av_register_output_format(&mjpeg_muxer);
#endif
#ifdef CONFIG_PCM_S16LE_DEMUXER
    av_register_input_format(&pcm_s16le_demuxer);
#endif
#ifdef CONFIG_PCM_S16LE_MUXER
    av_register_output_format(&pcm_s16le_muxer);
#endif
#ifdef CONFIG_PCM_S16BE_DEMUXER
    av_register_input_format(&pcm_s16be_demuxer);
#endif
#ifdef CONFIG_PCM_S16BE_MUXER
    av_register_output_format(&pcm_s16be_muxer);
#endif
#ifdef CONFIG_PCM_U16LE_DEMUXER
    av_register_input_format(&pcm_u16le_demuxer);
#endif
#ifdef CONFIG_PCM_U16LE_MUXER
    av_register_output_format(&pcm_u16le_muxer);
#endif
#ifdef CONFIG_PCM_U16BE_DEMUXER
    av_register_input_format(&pcm_u16be_demuxer);
#endif
#ifdef CONFIG_PCM_U16BE_MUXER
    av_register_output_format(&pcm_u16be_muxer);
#endif
#ifdef CONFIG_PCM_S8_DEMUXER
    av_register_input_format(&pcm_s8_demuxer);
#endif
#ifdef CONFIG_PCM_S8_MUXER
    av_register_output_format(&pcm_s8_muxer);
#endif
#ifdef CONFIG_PCM_U8_DEMUXER
    av_register_input_format(&pcm_u8_demuxer);
#endif
#ifdef CONFIG_PCM_U8_MUXER
    av_register_output_format(&pcm_u8_muxer);
#endif
#ifdef CONFIG_PCM_MULAW_DEMUXER
    av_register_input_format(&pcm_mulaw_demuxer);
#endif
#ifdef CONFIG_PCM_MULAW_MUXER
    av_register_output_format(&pcm_mulaw_muxer);
#endif
#ifdef CONFIG_PCM_ALAW_DEMUXER
    av_register_input_format(&pcm_alaw_demuxer);
#endif
#ifdef CONFIG_PCM_ALAW_MUXER
    av_register_output_format(&pcm_alaw_muxer);
#endif
#ifdef CONFIG_RAWVIDEO_DEMUXER
    av_register_input_format(&rawvideo_demuxer);
#endif
#ifdef CONFIG_RAWVIDEO_MUXER
    av_register_output_format(&rawvideo_muxer);
#endif
#ifdef CONFIG_NULL_MUXER
    av_register_output_format(&null_muxer);
#endif
#ifdef CONFIG_RM_DEMUXER
    av_register_input_format(&rm_demuxer);
#endif
#ifdef CONFIG_RM_MUXER
    av_register_output_format(&rm_muxer);
#endif
#ifdef CONFIG_NETWORK
#ifdef CONFIG_RTP_MUXER
    av_register_output_format(&rtp_muxer);
#endif
#ifdef CONFIG_RTSP_DEMUXER
    av_register_input_format(&rtsp_demuxer);
#endif
#ifdef CONFIG_SDP_DEMUXER
    av_register_input_format(&sdp_demuxer);
#endif
#ifdef CONFIG_REDIR_DEMUXER
    av_register_input_format(&redir_demuxer);
#endif
#endif /* CONFIG_NETWORK */
#ifdef CONFIG_SEGAFILM_DEMUXER
    av_register_input_format(&segafilm_demuxer);
#endif
#ifdef CONFIG_VMD_DEMUXER
    av_register_input_format(&vmd_demuxer);
#endif
#ifdef CONFIG_SMACKER_DEMUXER
    av_register_input_format(&smacker_demuxer);
#endif
#ifdef CONFIG_SOL_DEMUXER
    av_register_input_format(&sol_demuxer);
#endif
#ifdef CONFIG_SWF_DEMUXER
    av_register_input_format(&swf_demuxer);
#endif
#ifdef CONFIG_SWF_MUXER
    av_register_output_format(&swf_muxer);
#endif
#ifdef CONFIG_TTA_DEMUXER
    av_register_input_format(&tta_demuxer);
#endif
#ifdef CONFIG_VIDEO4LINUX2
#ifdef CONFIG_V4L2_DEMUXER
    av_register_input_format(&v4l2_demuxer);
#endif
#endif /* CONFIG_VIDEO4LINUX2 */
#if defined(CONFIG_VIDEO4LINUX) || defined(CONFIG_BKTR)
#ifdef CONFIG_VIDEO_GRAB_DEVICE_DEMUXER
    av_register_input_format(&video_grab_device_demuxer);
#endif
#endif /* CONFIG_VIDEO4LINUX || CONFIG_BKTR */
#ifdef CONFIG_VOC_DEMUXER
    av_register_input_format(&voc_demuxer);
#endif
#ifdef CONFIG_VOC_MUXER
    av_register_output_format(&voc_muxer);
#endif
#ifdef CONFIG_WAV_DEMUXER
    av_register_input_format(&wav_demuxer);
#endif
#ifdef CONFIG_WAV_MUXER
    av_register_output_format(&wav_muxer);
#endif
#ifdef CONFIG_WC3_DEMUXER
    av_register_input_format(&wc3_demuxer);
#endif
#ifdef CONFIG_WSAUD_DEMUXER
    av_register_input_format(&wsaud_demuxer);
#endif
#ifdef CONFIG_WSVQA_DEMUXER
    av_register_input_format(&wsvqa_demuxer);
#endif
#ifdef CONFIG_YUV4MPEGPIPE_MUXER
    av_register_output_format(&yuv4mpegpipe_muxer);
#endif
#ifdef CONFIG_YUV4MPEGPIPE_DEMUXER
    av_register_input_format(&yuv4mpegpipe_demuxer);
#endif

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
