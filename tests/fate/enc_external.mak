FATE_ENC_EXTERNAL-$(call ENCDEC, LIBX264 H264, MOV, H264_DEMUXER) += fate-libx264-simple
fate-libx264-simple: CMD = enc_external $(TARGET_SAMPLES)/h264-conformance/BA1_Sony_D.jsv \
    mp4 "-c:v libx264" "-show_entries frame=width,height,pix_fmt,pts,pkt_dts -of flat"

# test for SVT-AV1 MDCV and CLL passthrough during encoding
FATE_ENC_EXTERNAL-$(call ENCDEC, LIBSVTAV1 HEVC, MOV, HEVC_DEMUXER LIBDAV1D_DECODER) += fate-libsvtav1-hdr10
fate-libsvtav1-hdr10: CMD = enc_external $(TARGET_SAMPLES)/hevc/hdr10_plus_h265_sample.hevc \
    mp4 "-c:v libsvtav1" "-show_frames -show_entries frame=side_data_list -of flat"

# test for x264 MDCV and CLL passthrough during encoding
FATE_ENC_EXTERNAL-$(call ENCDEC, LIBX264 HEVC, MOV, LIBX264_HDR10 HEVC_DEMUXER H264_DECODER) += fate-libx264-hdr10
fate-libx264-hdr10: CMD = enc_external $(TARGET_SAMPLES)/hevc/hdr10_plus_h265_sample.hevc \
    mp4 "-c:v libx264" "-show_frames -show_entries frame=side_data_list -of flat"

FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_ENC_EXTERNAL-yes)
fate-enc-external: $(FATE_ENC_EXTERNAL-yes)
