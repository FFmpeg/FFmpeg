FATE_ENC_EXTERNAL-$(call ENCDEC, LIBX264 H264, MOV, H264_DEMUXER) += fate-libx264-simple
fate-libx264-simple: CMD = enc_external $(TARGET_SAMPLES)/h264-conformance/BA1_Sony_D.jsv \
    mp4 "-c:v libx264" "-show_entries frame=width,height,pix_fmt,pts,pkt_dts -of flat"

FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_ENC_EXTERNAL-yes)
fate-enc-external: $(FATE_ENC_EXTERNAL-yes)
