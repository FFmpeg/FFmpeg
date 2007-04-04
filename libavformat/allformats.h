/*
 * Register all the formats and protocols.
 * copyright (c) 2000, 2001, 2002 Fabrice Bellard
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

#ifndef ALLFORMATS_H
#define ALLFORMATS_H

extern AVInputFormat aac_demuxer;
extern AVInputFormat ac3_demuxer;
extern AVInputFormat aiff_demuxer;
extern AVInputFormat amr_demuxer;
extern AVInputFormat asf_demuxer;
extern AVInputFormat au_demuxer;
extern AVInputFormat audio_demuxer;
extern AVInputFormat avi_demuxer;
extern AVInputFormat avisynth_demuxer;
extern AVInputFormat avs_demuxer;
extern AVInputFormat daud_demuxer;
extern AVInputFormat dc1394_demuxer;
extern AVInputFormat dsicin_demuxer;
extern AVInputFormat dts_demuxer;
extern AVInputFormat dv1394_demuxer;
extern AVInputFormat dv_demuxer;
extern AVInputFormat dxa_demuxer;
extern AVInputFormat ea_demuxer;
extern AVInputFormat ffm_demuxer;
extern AVInputFormat flac_demuxer;
extern AVInputFormat flic_demuxer;
extern AVInputFormat flv_demuxer;
extern AVInputFormat fourxm_demuxer;
extern AVInputFormat gif_demuxer;
extern AVInputFormat gxf_demuxer;
extern AVInputFormat h261_demuxer;
extern AVInputFormat h263_demuxer;
extern AVInputFormat h264_demuxer;
extern AVInputFormat idcin_demuxer;
extern AVInputFormat image2_demuxer;
extern AVInputFormat image2pipe_demuxer;
extern AVInputFormat image_demuxer;
extern AVInputFormat imagepipe_demuxer;
extern AVInputFormat ingenient_demuxer;
extern AVInputFormat ipmovie_demuxer;
extern AVInputFormat libnut_demuxer;
extern AVInputFormat m4v_demuxer;
extern AVInputFormat matroska_demuxer;
extern AVInputFormat mjpeg_demuxer;
extern AVInputFormat mm_demuxer;
extern AVInputFormat mmf_demuxer;
extern AVInputFormat mov_demuxer;
extern AVInputFormat mp3_demuxer;
extern AVInputFormat mpc_demuxer;
extern AVInputFormat mpegps_demuxer;
extern AVInputFormat mpegts_demuxer;
extern AVInputFormat mpegvideo_demuxer;
extern AVInputFormat mtv_demuxer;
extern AVInputFormat mxf_demuxer;
extern AVInputFormat nsv_demuxer;
extern AVInputFormat nut_demuxer;
extern AVInputFormat nuv_demuxer;
extern AVInputFormat ogg_demuxer;
extern AVInputFormat pcm_alaw_demuxer;
extern AVInputFormat pcm_mulaw_demuxer;
extern AVInputFormat pcm_s16be_demuxer;
extern AVInputFormat pcm_s16le_demuxer;
extern AVInputFormat pcm_s8_demuxer;
extern AVInputFormat pcm_u16be_demuxer;
extern AVInputFormat pcm_u16le_demuxer;
extern AVInputFormat pcm_u8_demuxer;
extern AVInputFormat rawvideo_demuxer;
extern AVInputFormat redir_demuxer;
extern AVInputFormat rm_demuxer;
extern AVInputFormat roq_demuxer;
extern AVInputFormat sdp_demuxer;
extern AVInputFormat segafilm_demuxer;
extern AVInputFormat shorten_demuxer;
extern AVInputFormat smacker_demuxer;
extern AVInputFormat sol_demuxer;
extern AVInputFormat str_demuxer;
extern AVInputFormat swf_demuxer;
extern AVInputFormat thp_demuxer;
extern AVInputFormat tiertexseq_demuxer;
extern AVInputFormat tta_demuxer;
extern AVInputFormat v4l2_demuxer;
extern AVInputFormat vc1_demuxer;
extern AVInputFormat video_grab_bktr_demuxer;
extern AVInputFormat video_grab_v4l_demuxer;
extern AVInputFormat vmd_demuxer;
extern AVInputFormat voc_demuxer;
extern AVInputFormat wav_demuxer;
extern AVInputFormat wc3_demuxer;
extern AVInputFormat wsaud_demuxer;
extern AVInputFormat wsvqa_demuxer;
extern AVInputFormat wv_demuxer;
extern AVInputFormat x11_grab_device_demuxer;
extern AVInputFormat yuv4mpegpipe_demuxer;

extern AVOutputFormat ac3_muxer;
extern AVOutputFormat adts_muxer;
extern AVOutputFormat aiff_muxer;
extern AVOutputFormat amr_muxer;
extern AVOutputFormat asf_muxer;
extern AVOutputFormat asf_stream_muxer;
extern AVOutputFormat au_muxer;
extern AVOutputFormat audio_muxer;
extern AVOutputFormat avi_muxer;
extern AVOutputFormat crc_muxer;
extern AVOutputFormat dv_muxer;
extern AVOutputFormat ffm_muxer;
extern AVOutputFormat flac_muxer;
extern AVOutputFormat flv_muxer;
extern AVOutputFormat framecrc_muxer;
extern AVOutputFormat gif_muxer;
extern AVOutputFormat gxf_muxer;
extern AVOutputFormat h261_muxer;
extern AVOutputFormat h263_muxer;
extern AVOutputFormat h264_muxer;
extern AVOutputFormat image2_muxer;
extern AVOutputFormat image2pipe_muxer;
extern AVOutputFormat image_muxer;
extern AVOutputFormat imagepipe_muxer;
extern AVOutputFormat libnut_muxer;
extern AVOutputFormat m4v_muxer;
extern AVOutputFormat mjpeg_muxer;
extern AVOutputFormat mmf_muxer;
extern AVOutputFormat mov_muxer;
extern AVOutputFormat mp2_muxer;
extern AVOutputFormat mp3_muxer;
extern AVOutputFormat mp4_muxer;
extern AVOutputFormat mpeg1system_muxer;
extern AVOutputFormat mpeg1vcd_muxer;
extern AVOutputFormat mpeg1video_muxer;
extern AVOutputFormat mpeg2dvd_muxer;
extern AVOutputFormat mpeg2svcd_muxer;
extern AVOutputFormat mpeg2video_muxer;
extern AVOutputFormat mpeg2vob_muxer;
extern AVOutputFormat mpegts_muxer;
extern AVOutputFormat mpjpeg_muxer;
extern AVOutputFormat null_muxer;
extern AVOutputFormat ogg_muxer;
extern AVOutputFormat pcm_alaw_muxer;
extern AVOutputFormat pcm_mulaw_muxer;
extern AVOutputFormat pcm_s16be_muxer;
extern AVOutputFormat pcm_s16le_muxer;
extern AVOutputFormat pcm_s8_muxer;
extern AVOutputFormat pcm_u16be_muxer;
extern AVOutputFormat pcm_u16le_muxer;
extern AVOutputFormat pcm_u8_muxer;
extern AVOutputFormat psp_muxer;
extern AVOutputFormat rawvideo_muxer;
extern AVOutputFormat rm_muxer;
extern AVOutputFormat swf_muxer;
extern AVOutputFormat tg2_muxer;
extern AVOutputFormat tgp_muxer;
extern AVOutputFormat voc_muxer;
extern AVOutputFormat wav_muxer;
extern AVOutputFormat yuv4mpegpipe_muxer;

/* raw.c */
int pcm_read_seek(AVFormatContext *s,
                  int stream_index, int64_t timestamp, int flags);

/* rtsp.c */
int redir_open(AVFormatContext **ic_ptr, ByteIOContext *f);
/* rtp.c */
void av_register_rtp_dynamic_payload_handlers(void);

#endif
